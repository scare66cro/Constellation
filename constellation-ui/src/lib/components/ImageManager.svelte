<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import Button from '$lib/ui/Button.svelte';
  import TextField from '$lib/ui/TextField.svelte';
  import Table from '$lib/ui/Table.svelte';
  import Row from '$lib/ui/Row.svelte';
  import Column from '$lib/ui/Column.svelte';
  import { backgroundStore } from '$lib/store';
  import { KeyboardTypes } from '$lib/ui/Keyboard.svelte';
  import { t } from 'svelte-i18n';
	import { getHttpUrl } from '$lib/business/util';

  const dispatch = createEventDispatcher();

  export let visible = false;

  let uploadName = '';
  let selectedFile: File | null = null;
  let fileInput: HTMLInputElement;
  let uploadError = '';
  let editingId = '';
  let editingName = '';

  $: customImages = $backgroundStore.customImages || [];

  // Load background pictures from IoT API when component mounts
  onMount(async () => {
    await backgroundStore.loadPictures();
  });

  // Reactive statement to watch visible changes and reload pictures
  $: if (visible) {
    backgroundStore.loadPictures();
  }
  async function resizeImage(file: File): Promise<File> {
    return new Promise((resolve) => {
      const img = new Image();
      const canvas = document.createElement('canvas');
      const ctx = canvas.getContext('2d')!;
      
      img.onload = () => {
        // Calculate new dimensions while maintaining aspect ratio
        const maxWidth = 1920;
        const maxHeight = 1080;
        
        let { width, height } = img;
        
        // Calculate scaling factor
        const widthRatio = maxWidth / width;
        const heightRatio = maxHeight / height;
        const scale = Math.min(widthRatio, heightRatio, 1); // Don't upscale
        
        const newWidth = Math.round(width * scale);
        const newHeight = Math.round(height * scale);
        
        // Set canvas dimensions
        canvas.width = newWidth;
        canvas.height = newHeight;
        
        // Draw resized image
        ctx.drawImage(img, 0, 0, newWidth, newHeight);
        
        // Convert to blob and create new File
        canvas.toBlob((blob) => {
          if (blob) {
            const resizedFile = new File([blob], file.name, {
              type: file.type,
              lastModified: Date.now(),
            });
            resolve(resizedFile);
          } else {
            resolve(file); // Fallback to original if conversion fails
          }
        }, file.type, 0.9); // 90% quality for JPEG compression
      };
      
      const originalOnload = img.onload;
      const originalOnerror = img.onerror;
      
      img.onload = (event) => {
        // Calculate new dimensions while maintaining aspect ratio
        const maxWidth = 1920;
        const maxHeight = 1080;
        
        let { width, height } = img;
        
        // Calculate scaling factor
        const widthRatio = maxWidth / width;
        const heightRatio = maxHeight / height;
        const scale = Math.min(widthRatio, heightRatio, 1); // Don't upscale
        
        const newWidth = Math.round(width * scale);
        const newHeight = Math.round(height * scale);
        
        // Set canvas dimensions
        canvas.width = newWidth;
        canvas.height = newHeight;
        
        // Draw resized image
        ctx.drawImage(img, 0, 0, newWidth, newHeight);
        
        // Convert to blob and create new File
        canvas.toBlob((blob) => {
          if (blob) {
            const resizedFile = new File([blob], file.name, {
              type: file.type,
              lastModified: Date.now(),
            });
            resolve(resizedFile);
          } else {
            resolve(file); // Fallback to original if conversion fails
          }
        }, file.type, 0.9); // 90% quality for JPEG compression
        
        URL.revokeObjectURL(blobUrl);
        if (originalOnload) originalOnload.call(img, event);
      };
      
      img.onerror = (event) => {
        URL.revokeObjectURL(blobUrl);
        if (originalOnerror) originalOnerror.call(img, event);
        resolve(file); // Fallback to original if image load fails
      };
      
      const blobUrl = URL.createObjectURL(file);
      img.src = blobUrl;
    });
  }

  async function handleFileSelect(event: Event) {
    const target = event.target as HTMLInputElement;
    const file = target.files?.[0];
    
    if (file) {
      // Validate file type
      if (!file.type.startsWith('image/')) {
        uploadError = 'Please select an image file';
        return;
      }
      
      // Validate file size (max 10MB)
      if (file.size > 10 * 1024 * 1024) {
        uploadError = 'File size must be less than 10MB';
        return;
      }
      
      try {
        // Resize the image before setting it as selected file
        selectedFile = await resizeImage(file);
        uploadError = '';
        
        // Auto-generate name from filename if empty
        if (!uploadName.trim()) {
          uploadName = file.name.replace(/\.[^/.]+$/, ''); // Remove extension
        }
      } catch (error) {
        console.error('Error resizing image:', error);
        uploadError = 'Failed to process image';
      }
    }
  }

  async function handleUpload() {
    if (!selectedFile || !uploadName.trim()) {
      uploadError = 'Please provide both a name and select a file';
      return;
    }

    try {
      // Create FormData for file upload
      const formData = new FormData();
      formData.append('file', selectedFile);
      formData.append('displayName', uploadName.trim());

      // Upload file using new IoT API
      const iotResponse = await fetch(getHttpUrl('/iot/background-pictures'), {
        method: 'POST',
        body: formData
      });

      if (!iotResponse.ok) {
        const errorData = await iotResponse.json().catch(() => ({ error: 'Unknown error' }));
        uploadError = errorData.error || `Upload failed: ${iotResponse.status} ${iotResponse.statusText}`;
        console.error('Upload failed:', errorData);
        return;
      }
      const iotResult = await iotResponse.json();
      if (iotResult.id && iotResult.filename && iotResult.displayName) {
        // Reload all pictures from the API to ensure we have the latest data
        await backgroundStore.loadPictures();

        // Reset form
        uploadName = '';
        selectedFile = null;
        uploadError = '';
        if (fileInput) fileInput.value = '';

        dispatch('uploaded');
      } else {
        uploadError = 'Upload failed: Invalid response format';
        console.error('Invalid upload response format:', iotResult);
      }
    } catch (error) {
      const errorMessage = error instanceof Error ? error.message : 'Failed to upload image';
      uploadError = `Network error: ${errorMessage}`;
      console.error('Upload error:', error);
    }
  }

  function startEditing(id: string, currentName: string) {
    editingId = id;
    editingName = currentName;
  }
  async function saveEdit() {
    if (editingName.trim() && editingId) {
      try {
        // Update display name using new IoT API
        const response = await fetch(getHttpUrl(`/iot/background-pictures/${editingId}`), {
          method: 'PUT',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({ displayName: editingName.trim() })
        });if (response.ok) {
          const result = await response.json();

          // Reload all pictures from the API
          await backgroundStore.loadPictures();
        } else {
          const errorData = await response.json().catch(() => ({ error: 'Unknown error' }));
          console.error('Failed to update display name on server:', errorData);
        }
      } catch (error) {
        console.error('Update error:', error);
      }

      editingId = '';
      editingName = '';
    }
  }

  function cancelEdit() {
    editingId = '';
    editingName = '';
  }

  async function deleteImage(id: string) {
    if (confirm($t('level1.miscellaneous.confirm-delete-image'))) {
      try {
        // Delete using new IoT API
        const response = await fetch(getHttpUrl(`/iot/background-pictures/${id}`), {
          method: 'DELETE'
        });
        if (response.ok) {
          const result = await response.json();

          // Reload all pictures from the API
          await backgroundStore.loadPictures();
        } else {
          const errorData = await response.json().catch(() => ({ error: 'Unknown error' }));
          console.error('Failed to delete file from server:', errorData);
        }
      } catch (error) {
        console.error('Delete error:', error);
      }
    }
  }

  function close() {
    visible = false;
    editingId = '';
    editingName = '';
    uploadError = '';
    dispatch('close');
  }
</script>

{#if visible}
  <div class="fixed inset-0 bg-grey-500 flex items-center justify-center z-50">
    <div class="container-wide max-h-[90vh] overflow-y-auto bg-gray-300 shadow-md md:shadow-lg shadow-gray-500 text-size rounded-lg">
      <div class="flex justify-between items-center mb-6 bg-primary-900 text-white rounded-t-lg">
        <h2 class="text-3xl font-bold ml-4">{$t('level1.miscellaneous.manage-background-images')}</h2>
        <Button on:click={close} class="text-white !ring-primary-900 !hover:bg-primary-900" noFocus={true}>✕</Button>
      </div>

      <!-- Upload Section -->
      <div class="m-8 p-4 border rounded-lg bg-gray-50">
        <h3 class="text-size-xl font-semibold mb-4">{$t('level1.miscellaneous.upload-new-image')}</h3>
        <Table class="mb-4">
          <Row class="flex flex-row items-center">
            <Column class="w-3/12 px-2">
              <label for="image-name-input" class="block font-medium mb-2">
                {$t('level1.miscellaneous.image-name')}:
              </label>
              <TextField
                id="image-name-input"
                size="xl"
                bind:value={uploadName}
                keyboardType={KeyboardTypes.Alpha}
                edit={true}
                placeholder="Enter image name"
                class="w-full"
              />
            </Column>
            <Column class="w-7/12">
              <div class="flex items-center gap-6">
                <!-- Custom file upload button: native input visually hidden to allow full styling control -->
                <label class="group relative flex items-center justify-center select-none cursor-pointer rounded-2xl bg-primary-900 hover:bg-primary-800 active:bg-primary-700 text-white text-size-large font-bold mx-2 py-4 px-8 shadow-xl transition focus-within:ring-4 focus-within:ring-primary-400 focus-within:ring-offset-2 focus-within:ring-offset-gray-100">
                  <span class="pointer-events-none whitespace-nowrap">{$t('level1.miscellaneous.select-file')}</span>
                  <input
                    id="file-input"
                    bind:this={fileInput}
                    type="file"
                    accept="image/*"
                    on:change={handleFileSelect}
                    class="sr-only"
                    aria-label={$t('level1.miscellaneous.select-file')}
                  />
                </label>
                {#if selectedFile}
                  <span
                    class="text-size-large font-medium text-gray-800 truncate"
                    title={selectedFile.name}
                    aria-live="polite"
                  >
                    {selectedFile.name}
                  </span>
                {/if}
              </div>
            </Column>
            <Column class="w-2/12">
              <Button
                class="ml-auto mr-4"
                size="xl"
                on:click={handleUpload}
                disabled={!selectedFile || !uploadName.trim()}
              >
                {$t('level1.miscellaneous.upload')}
              </Button>
            </Column>
          </Row>
        </Table>

        {#if uploadError}
          <div class="text-red-600 text-lg mb-2">{uploadError}</div>
        {/if}

        {#if selectedFile}
          <div class="text-lg text-gray-600">
            Selected: {selectedFile.name} ({(selectedFile.size / 1024 / 1024).toFixed(2)} MB)
          </div>
        {/if}
      </div>

      <!-- Custom Images List -->
      <div class="p-8">
        <h3 class="text-size-xl font-semibold mb-4">{$t('level1.miscellaneous.custom-images')}</h3>
        
        {#if customImages.length === 0}
          <div class="text-gray-500 text-center py-8">
            {$t('level1.miscellaneous.no-custom-images')}
          </div>
        {:else}
          <Table>
            {#each customImages as image (image.id)}
              <Row class="border-b">
                <Column class="w-1/4 p-2">
                  <div class="w-16 h-16 rounded overflow-hidden bg-gray-200">
                    <img 
                      src={image.value} 
                      alt={image.text} 
                      class="w-full h-full object-cover"
                      on:error={() => {
                        // Handle broken image
                        console.warn('Failed to load image:', image.value);
                      }}
                    />
                  </div>
                </Column>
                <Column class="w-1/2">
                  {#if editingId === image.id}
                    <div class="flex gap-2">
                      <TextField
                        size="xl"
                        bind:value={editingName}
                        keyboardType={KeyboardTypes.Alpha}
                        edit={true}
                      />
                      <Button size="lg" on:click={saveEdit} class="bg-green-600 text-white mr-2">
                        ✓
                      </Button>
                      <Button size="lg" on:click={cancelEdit} class="bg-gray-600 text-white mr-2">
                        ✕
                      </Button>
                    </div>
                  {:else}
                    <div class="text-size-large font-medium">{image.text}</div>
                  {/if}
                </Column>
                <Column class="w-1/4">
                  {#if editingId !== image.id}
                    <Button
                      class="ml-auto mr-2"
                      size="lg"
                      on:click={() => startEditing(image.id, image.text)}
                    >
                      {$t('global.edit')}
                    </Button>
                    <Button
                      size="lg"
                      on:click={() => deleteImage(image.id)}
                      class="!bg-secondary-700 !hover:bg-secondary-500 text-white mr-2"
                    >
                      {$t('global.delete')}
                    </Button>
                  {/if}
                </Column>
              </Row>
            {/each}
          </Table>
        {/if}
      </div>

      <!-- Close Button -->
      <div class="flex justify-end mt-6">
        <Button class="mr-4" size="lg" on:click={close}>
          {$t('global.close')}
        </Button>
      </div>
    </div>
  </div>
{/if}

<style>
  /* Custom styles for modal */
  :global(.fixed) {
    position: fixed;
  }
</style>

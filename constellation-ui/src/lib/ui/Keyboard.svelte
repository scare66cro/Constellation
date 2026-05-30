<script lang="ts" context="module">
  export enum KeyboardTypes {
    None,
    Alpha,
    Numeric,
    Float,
    Auto,
  };
</script>

<script lang="ts">
  import Keyboard from 'simple-keyboard';
  import { onMount, tick, createEventDispatcher } from 'svelte';
  import { t } from "svelte-i18n";
  import { frontMatterStore } from '$lib/store';
  import { accountSettings } from '$lib/business/protoStores';
	import Select from './Select.svelte';
	import { getHttpUrl, safeJsonParse, isRebooting } from '$lib/business/util';

  const dispatch = createEventDispatcher();

  export let label: string;

  export let hidden = true;

  export let keyboardType: KeyboardTypes = KeyboardTypes.None;

  export let start = '';

  export let inputType = 'text';

  $: input = (inputType === 'text' && keyboardType === KeyboardTypes.Alpha) ? start : '';

  let alphaKeyboard: Keyboard;

  let numKeyboard: Keyboard;

  let autoKeyboard: Keyboard;

  let floatKeyboard: Keyboard;

  let activeKeyboard: Keyboard;

  let alphaRef: HTMLDivElement;

  let inputRef: HTMLInputElement;

  let numRef: HTMLDivElement;

  let floatRef: HTMLDivElement;

  let autoRef: HTMLDivElement;

  let hiddenAlpha = true;

  let hiddenNum = true;

  let hiddenAuto = true;

  let hiddenFloat = true;

  $: currentKeyboardType = '';
  $: user = 'DEFAULT';
  // Login user list comes from the AccountSettings proto store (envelope
  // tag 33). Reactive — the dropdown re-populates whenever the firmware
  // pushes an updated user roster, no fetch/retry needed.
  $: users = (($accountSettings?.users ?? []) as Array<{ slot: number; userId: string }>) 
    .map((u) => ({ text: u.userId, value: u.userId }))
    .filter((u) => u.value !== '');

  $: init = !hidden;
  $: initPassword = !hidden;

  // When keyboard is hidden, proactively blur the active input to allow
  // password manager / autofill overlays to detach and avoid async handlers
  // in extensions referencing a now-removed element (which produced the
  // "Cannot set properties of null (setting 'newPassword')" error).
  $: if (hidden && inputRef) {
    // Use setTimeout to ensure any pending key handlers finish first
    setTimeout(() => inputRef?.blur(), 0);
  }

  $: {
    hiddenAlpha = keyboardType !== KeyboardTypes.Alpha;
    hiddenNum = keyboardType !== KeyboardTypes.Numeric;
    hiddenFloat = keyboardType !== KeyboardTypes.Float;
    hiddenAuto = keyboardType !== KeyboardTypes.Auto;
    switch (keyboardType) {
      case KeyboardTypes.Alpha:
        activeKeyboard = alphaKeyboard;
        break;
      case KeyboardTypes.Numeric:
        activeKeyboard = numKeyboard;
        break;
      case KeyboardTypes.Float:
        activeKeyboard = floatKeyboard;
        break;
      case KeyboardTypes.Auto:
        activeKeyboard = autoKeyboard;
        break;
    }
  }

  $: {
    activeKeyboard?.setInput(input);
    if (init) {
      // Set cursor to end of input instead of selecting all text for better touch experience
      const cursorPos = input.length;
      activeKeyboard?.setCaretPosition(cursorPos);
      let newLayout =  $frontMatterStore.keyboardType === '1' ? 'alpha' : 'default';
      // if not alpha keyboard, set to default
      if (activeKeyboard !== alphaKeyboard) {
        newLayout = 'default';
      }
      activeKeyboard.setOptions({
        layoutName: newLayout,
      });

      tick().then(() => {
        inputRef?.focus();
        // Set cursor position after focus to prevent jumping to beginning
        inputRef?.setSelectionRange(cursorPos, cursorPos);
        init = false;
      });
    }
  }

  // Phase 5.1 cleanup (S9d): legacy /iot/accounts fetch + retry loop
  // removed; `users` derives reactively from $accountSettings above.

  onMount(async () => {
    alphaKeyboard = new Keyboard('.alphaKeyboard', {
      onChange,
      onKeyPress,
      preventMouseDownDefault: true,
      preventMouseUpDefault: true,
      stopMouseDownPropagation: true,
      stopMouseUpPropagation: true,
      layout: {
        default: [
          " @ #     * ( ) - _",
          "1 2 3 4 5 6 7 8 9 0 {bksp}",
          "Q W E R T Y U I O P {clear}",
          " A S D F G H J K L {enter}",
          "  Z X C V B N M , .",
          "{lower} {alpha} {space} {close}"
        ],
        shift:  [
          " @ #     * ( ) - _",
          "1 2 3 4 5 6 7 8 9 0 {bksp}",
          "q w e r t y u i o p {clear}",
          " a s d f g h j k l {enter}",
          "  z x c v b n m , .",
          "{upper} {alpha} {space} {close}"
        ],
        alpha: [
          " @ # , . * ( ) - _ ",
          "1 2 3 4 5 6 7 8 9 0",
          "A B C D E F G H I J",
          "K L M N O P Q R S T",
          " U V W X Y Z",
          "{bksp} {clear} {space} {enter}",
          " {lower} {close} {standard}"
        ],
        alpha_shift:  [
          " @ # , . * ( ) - _ ",
          "1 2 3 4 5 6 7 8 9 0",
          "a b c d e f g h i j",
          "k l m n o p q r s t",
          " u v w x y z",
          "{bksp} {clear} {space} {enter}",
          " {upper} {close} {standard}"
        ]
      },
      display: {
        '{bksp}': 'Back',
        '{close}': 'Close',
        '{enter}': 'Enter',
        '{clear}': 'Clear',
        '{lower}': 'Lowercase',
        '{upper}': 'Uppercase',
        '{space}': 'Space',
        '{alpha}': 'Alpha',
        '{standard}': 'Standard'
      },
      theme: "hg-theme-default keyboard-font-size",
      inputName: 'keyboardInput',
    });
    numKeyboard = new Keyboard('.numKeyboard', {
      onChange,
      onKeyPress,
      preventMouseDownDefault: true,
      preventMouseUpDefault: true,
      stopMouseDownPropagation: true,
      stopMouseUpPropagation: true,
      layout: {
        default: ["7 8 9", "4 5 6", "1 2 3", " 0 ", "{bksp} {enter} {close}"],
      },
      display: {
        '{bksp}': 'Back',
        '{close}': 'Close',
        '{enter}': 'Enter',
        '{plusminus}': '+/-',
      },
      theme: "hg-theme-default hg-layout-numeric numeric-theme keyboard-font-size",
      inputName: 'keyboardInput',
    });
    autoKeyboard = new Keyboard('.autoKeyboard', {
      onChange,
      onKeyPress,
      preventMouseDownDefault: true,
      preventMouseUpDefault: true,
      stopMouseDownPropagation: true,
      stopMouseUpPropagation: true,
      layout: {
        default: ["7 8 9", "4 5 6", "1 2 3", "0 Auto", "{bksp} {enter} {close}"],
      },
      display: {
        '{bksp}': 'Back',
        '{close}': 'Close',
        '{enter}': 'Enter',
        '{plusminus}': '+/-',
        'Auto': $t('global.auto'),
      },
      theme: "hg-theme-default hg-layout-numeric numeric-theme keyboard-font-size",
      inputName: 'keyboardInput',
    });
    floatKeyboard = new Keyboard('.floatKeyboard', {
      onChange,
      onKeyPress,
      preventMouseDownDefault: true,
      preventMouseUpDefault: true,
      stopMouseDownPropagation: true,
      stopMouseUpPropagation: true,
      layout: {
        default: ["7 8 9", "4 5 6", "1 2 3", "0 {plusminus} .", "{bksp} {enter} {close}"]
      },
      display: {
        '{bksp}': 'Back',
        '{close}': 'Close',
        '{enter}': 'Enter',
        '{plusminus}': '+/-',
      },
      theme: "hg-theme-default hg-layout-numeric numeric-theme keyboard-font-size",
      inputName: 'keyboardInput',
    });
    // Initialize cursor position to end of input rather than beginning
    const initialCursorPos = input.length;
    alphaKeyboard.setCaretPosition(initialCursorPos);
    numKeyboard.setCaretPosition(initialCursorPos);
    floatKeyboard.setCaretPosition(initialCursorPos);
    await tick();
    if (keyboardType === KeyboardTypes.Numeric) {
      numRef.focus();
    } else if (keyboardType == KeyboardTypes.Float) {
      floatRef.focus();
    } else if (keyboardType === KeyboardTypes.Alpha) {
      alphaRef.focus();
    }

    currentKeyboardType = $frontMatterStore.keyboardType as string;
  });

  async function onChange(val: string): Promise<void> {
    input = val;
    await tick();
    syncCaretPosition();
  }

  function syncCaretPosition() {
    let caretPosition = activeKeyboard.caretPosition;
    if (caretPosition === null || caretPosition === undefined) {
      caretPosition = input.length; // Default to end of input
      activeKeyboard.setCaretPosition(caretPosition);
    }
    if (caretPosition !== undefined && inputRef?.setSelectionRange) {
      // Use setTimeout to ensure the input is focused before setting cursor position
      // This helps prevent cursor jumping to beginning in touch mode
      setTimeout(() => {
        if (inputRef) {
          inputRef.focus();
          inputRef.setSelectionRange(caretPosition, caretPosition);
        }
      }, 0);
    }
  }

  function onKeyPress(button: string): void {
    if (button === '{lower}' || button === '{upper}') {
      handleShift();
    }
    let currentLayout, newLayout;
    switch (button) {
      case '{enter}':
        if (inputType === 'loginPassword') {
          const currentUser = (users?.length > 1 && user && user !== '') ? user : 'DEFAULT';
          dispatch('result-available', `${currentUser}:${input}`);
        } else if (inputType === 'password') {
          dispatch('result-available', input);
        } else {
          if (input === $t('global.auto')) {
            input = $t('global.automatically')
          }
          dispatch('result-available', input);
        }
        clear();
        break;
      case '{standard}':
        currentLayout = activeKeyboard.options.layoutName;
        if (currentLayout === 'alpha') {
          newLayout = 'default';
        } else if (currentLayout === 'alpha_shift') {
          newLayout = 'shift';
        }
        activeKeyboard.setOptions({
          layoutName: newLayout,
        });
        break;
      case '{alpha}':
        currentLayout = activeKeyboard.options.layoutName;
        if (currentLayout === 'default') {
          newLayout = 'alpha';
        } else if (currentLayout === 'shift') {
          newLayout = 'alpha_shift';
        }
        activeKeyboard.setOptions({
          layoutName: newLayout,
        });
        break;
      case '{close}':
        dispatch('close-keyboard');
        // fallthrough
      case '{clear}':
        clear();
        break;
      case '{plusminus}':
        if (input.startsWith('-')) {
          input = input.substring(1);
        } else {
          input = '-' + input;
        }
        break;
      case 'Auto':
        input = $t('global.auto');
        break;
    }
  }

  function clear(): void {
    input = '';
    start = '';
  }

  function handleShift(): void {
    const currentLayout = activeKeyboard.options.layoutName;
    let shiftToggle;
    if (currentLayout === 'default') {
      shiftToggle = 'shift';
    } else if (currentLayout === 'shift') {
      shiftToggle = 'default';
    } else if (currentLayout === 'alpha') {
      shiftToggle = 'alpha_shift';
    } else if (currentLayout === 'alpha_shift') {
      shiftToggle = 'alpha';
    }
    activeKeyboard.setOptions({
      layoutName: shiftToggle,
    });
  }

  function onInput(event: Event): void {
    activeKeyboard.setInput((event.target as HTMLInputElement).value);
  }

  function handlePasswordKeyDown(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      if (inputType === 'loginPassword') {
        const currentUser = (users?.length > 1 && user && user !== '') ? user : 'DEFAULT';
        dispatch('result-available', `${currentUser}:${input}`);
      } else {
        dispatch('result-available', input);
      }
      clear();
    }
  }

</script>

<div class:hidden={hidden} class="min-w-screen h-screen animated fadeIn faster fixed left-0 top-0 flex justify-center items-center inset-0 z-50 outline-none focus:outline-none">
  <div class="relative mx-auto my-auto rounded-md shadow-md bg-primary-900 text-white" class:alpha={keyboardType === KeyboardTypes.Alpha}>
    <div class="w-full px-3 py-2 text-size-large">{label}</div>
    <div class="rounded-md shadow-md rounded-t-none bg-white text-black p-4">
      <div class="w-full flex flex-row">
        {#if !hiddenNum || !hiddenFloat}
            <input name="Previous Value"
            disabled
            type="text"
            value={start}
            class="block text-size-xl w-1/2 mr-2 mb-2 appearance-none focus:outline-none border-none text-center"
          />
        {/if}
        {#if inputType === 'password' || inputType === 'loginPassword'}
          {#if inputType === 'loginPassword' && users.length > 1}
            <Select class="w-64 mr-2" bind:value={user} options={users} edit={true} size="xl" />
          {/if}
          <input
            name="Input"
            type="password"
            placeholder=" "
            autocomplete="off"
            autocorrect="off"
            autocapitalize="none"
            spellcheck="false"
            inputmode="none"
            data-form-type="other"
            data-lpignore="true"
            data-1p-ignore="true"
            data-bwignore="true"
            aria-autocomplete="none"
            readonly
            on:focus={() => {
              inputRef.removeAttribute('readonly');
              // Ensure cursor is at end after focus in touch mode
              setTimeout(() => {
                const cursorPos = input.length;
                inputRef?.setSelectionRange(cursorPos, cursorPos);
              }, 0);
            }}
            bind:value={input}
            class="peer block text-size-xl w-{(hiddenNum && hiddenFloat) ? 'full' : '1/2'} appearance-none focus:outline-none mb-2"
            bind:this={inputRef}
            on:input={onInput}
            on:keydown={handlePasswordKeyDown}
          />
        {:else}
          <input
            name="Input"
            type="text"
            placeholder=" "
            bind:value={input}
            class="peer block text-size-xl w-{(hiddenNum && hiddenFloat) ? 'full' : '1/2'} appearance-none focus:outline-none mb-2"
            bind:this={inputRef}
            on:input={onInput}
            on:keypress={(e) => { if (e.key === 'Enter') { dispatch('result-available', input); clear(); }}}
          />
        {/if}
      </div>
      {#if inputType === 'loginPassword'}
        <div class="flex justify-end mb-2">
          <button type="button"
            class="text-sm text-primary-700 underline hover:text-primary-900"
            on:click={() => dispatch('cloud-login')}>
            Sign in with cloud account
          </button>
        </div>
      {/if}
      <!-- svelte-ignore a11y-click-events-have-key-events a11y-no-static-element-interactions -->
      <div class:hidden={hiddenAlpha} class="h-full">
        <div class="alphaKeyboard" bind:this={alphaRef}></div>
      </div>
      <!-- svelte-ignore a11y-click-events-have-key-events a11y-no-static-element-interactions -->
      <div class:hidden={hiddenNum}>
        <div class="numKeyboard" bind:this={numRef}></div>
      </div>
      <!-- svelte-ignore a11y-click-events-have-key-events a11y-no-static-element-interactions -->
      <div class:hidden={hiddenFloat}>
        <div class="floatKeyboard" bind:this={floatRef}></div>
      </div>
      <!-- svelte-ignore a11y-click-events-have-key-events a11y-no-static-element-interactions -->
      <div class:hidden={hiddenAuto}>
        <div class="autoKeyboard" bind:this={autoRef}></div>
      </div>
    </div>
  </div>
</div>
<div class:hidden={hidden} class="opacity-50 fixed inset-0 z-40 bg-black"></div>

<style lang="postcss">
  .alpha {
    @apply w-11/12;
  }

  :global(.keyboard-font-size) {
    @apply text-xl xl:text-4xl 3xl:text-6xl;
  }
  /* Remove fixed height from simple-keyboard buttons */
  :global(.hg-theme-default .hg-button) {
    @apply !h-auto;
  }
</style>
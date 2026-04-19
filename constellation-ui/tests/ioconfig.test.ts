import { test, expect } from '@playwright/test';

// Utility to build a minimal mock IO config dataset (equipment index -> pid mapping arrays)
function buildConfig() {
  return {
    outputConfig: Array(50).fill(''), // allocate space for equipment indices
    inputConfig: Array(50).fill('')
  };
}

// Shared HTML template implementing simplified ioConfig save + validation + dirty tracking logic
const pageHtml = `<!DOCTYPE html><html><head><title>IOConfig Test</title></head>
<body>
  <h1 id="title">System I/O Configuration</h1>
  <div>
    <label>Slot 1 Output
      <select id="o1">
        <option value="-1">None</option>
        <option value="0">Equip 0</option>
        <option value="1">Equip 1</option>
      </select>
    </label>
    <span id="o1-msg" class="msg"></span>
  </div>
  <div>
    <label>Slot 2 Output
      <select id="o2">
        <option value="-1">None</option>
        <option value="0">Equip 0</option>
        <option value="1">Equip 1</option>
      </select>
    </label>
    <span id="o2-msg" class="msg"></span>
  </div>
  <div>
    <label>Slot 1 Input
      <select id="i1">
        <option value="-1">None</option>
        <option value="0">Equip 0</option>
        <option value="1">Equip 1</option>
      </select>
    </label>
    <span id="i1-msg" class="msg"></span>
  </div>
  <button id="save">Save</button>
  <div id="status"></div>
  <script>
    // Minimal navigationStore mock with dirty tracking similar to app logic
    window.navigationStore = { isDirty: () => false };
    const original = { config: { outputConfig: [], inputConfig: [] } };
    const ioConfig = { config: { outputConfig: [], inputConfig: [] } };
    // Raw per-slot mapping like pendingOutput/pendingInput (index = pid) storing equipment index
    const raw = { output: [], input: [] };

    function updateDirtyChecker() {
      window.navigationStore.isDirty = () => {
        return JSON.stringify(ioConfig.config.outputConfig) !== JSON.stringify(original.config.outputConfig) ||
               JSON.stringify(ioConfig.config.inputConfig) !== JSON.stringify(original.config.inputConfig);
      };
    }

    function mapSelection(slotId, equipIdx, type) {
      const pid = slotId; // here slotId numeric also used as pid for simplicity
      if (type === 'o') {
        raw.output[pid] = equipIdx;
        // equipment index -> pid mapping
        if (!isNaN(parseInt(equipIdx))) {
          ioConfig.config.outputConfig[parseInt(equipIdx)] = pid.toString();
        }
      } else {
        raw.input[pid] = equipIdx;
        if (!isNaN(parseInt(equipIdx))) {
          ioConfig.config.inputConfig[parseInt(equipIdx)] = pid.toString();
        }
      }
      updateDirtyChecker();
    }

    document.getElementById('o1').addEventListener('change', (e) => mapSelection(1, e.target.value, 'o'));
    document.getElementById('o2').addEventListener('change', (e) => mapSelection(2, e.target.value, 'o'));
    document.getElementById('i1').addEventListener('change', (e) => mapSelection(1, e.target.value, 'i'));

    async function save() {
      document.getElementById('status').textContent = 'Saving...';
      // Clear validation
      ['o1-msg','o2-msg','i1-msg'].forEach(id => document.getElementById(id).textContent='');
      const body = JSON.stringify({ outputConfig: ioConfig.config.outputConfig, inputConfig: ioConfig.config.inputConfig, output: raw.output, input: raw.input });
      // Use absolute URL so that running inside setContent (about:blank origin) still matches Playwright route '**/iot/ioconfig'
      const resp = await fetch('http://localhost/ioconfig/iot/ioconfig'.replace('/ioconfig/iot','/iot'), { method: 'POST', headers: { 'Content-Type':'application/json' }, body });
      try {
        const json = await resp.json();
        if (json?.data?.Type === 'Validation') {
          const errs = json.data.errors || {};
            (errs.Duplicates||[]).forEach(cell => {
              const el = document.getElementById(cell + '-msg');
              if (el) el.textContent = 'Duplicate selection';
            });
          document.getElementById('status').textContent = 'Validation Failed';
        } else {
          // Success: update baseline
          original.config.outputConfig = JSON.parse(JSON.stringify(ioConfig.config.outputConfig));
          original.config.inputConfig = JSON.parse(JSON.stringify(ioConfig.config.inputConfig));
          updateDirtyChecker();
          document.getElementById('status').textContent = 'Saved';
        }
      } catch {
        document.getElementById('status').textContent = 'Error';
      }
    }
    document.getElementById('save').addEventListener('click', save);
    updateDirtyChecker();
  </script>
</body></html>`;

test.describe('IOConfig Saving & Validation', () => {
  test('dirty bit set on change and cleared after save', async ({ page }) => {
    await page.route('**/iot/ioconfig', async route => {
      // Always succeed
      await route.fulfill({ status: 200, body: JSON.stringify({ status: 200, data: { data: {} } }), contentType: 'application/json' });
    });

    await page.setContent(pageHtml);

    // Initially not dirty
    let dirty = await page.evaluate(() => window.navigationStore.isDirty());
    expect(dirty).toBe(false);

    // Change an output select
    await page.selectOption('#o1', '1');
    dirty = await page.evaluate(() => window.navigationStore.isDirty());
    expect(dirty).toBe(true);

  // Save
  await page.click('#save');
  await expect(page.locator('#status')).toHaveText('Saved');
    dirty = await page.evaluate(() => window.navigationStore.isDirty());
    expect(dirty).toBe(false);
  });

  test('duplicate validation error displayed', async ({ page }) => {
    await page.route('**/iot/ioconfig', async route => {
      // Return validation error for duplicates
      await route.fulfill({ status: 200, body: JSON.stringify({ status: 200, data: { Type: 'Validation', errors: { Duplicates: ['o1','i1'] } } }), contentType: 'application/json' });
    });
    await page.setContent(pageHtml);

    // Select same equipment for output and input to conceptually cause duplicate (frontend just sends data)
    await page.selectOption('#o1', '0');
    await page.selectOption('#i1', '0');
    await page.click('#save');

    // Expect validation messages
    await expect(page.locator('#o1-msg')).toHaveText('Duplicate selection');
    await expect(page.locator('#i1-msg')).toHaveText('Duplicate selection');
    await expect(page.locator('#status')).toHaveText('Validation Failed');
  });
});

/* nova_warnings.c
 *
 * Nova-native implementation of the warning store.
 *
 * MIGRATION STATUS (Phase 1 of legacy → Nova-native):
 *   This module replaces docs/legacy_AS2_reference/Application/Warnings.c
 *   in the Nova firmware build.  See /memories/repo/legacy-migration-plan.md
 *   for the broader migration plan.
 *
 *   The C API is unchanged from legacy Warnings.h so all existing callers
 *   (Analog_Input.c, Controls.c, LoadLogs.c, PIDLogs.c, SystemLogs.c,
 *   StorePostData.c, etc. — still compiled from the read-only legacy
 *   reference tree) continue to link without source changes.  Only the
 *   .c file is replaced.  Once Failures.c, Controls.c, etc. migrate
 *   Nova-side they will keep using this same API.
 *
 * Differences vs legacy:
 *   • WarningsSendToUI() is a no-op stub.  The legacy ASCII multi-message
 *     UI bridge is dead in Nova; warning broadcast happens through
 *     NovaMsg_SendWarnings() driven by NovaDataExc_Tick() poll-detection.
 *   • The Warning[] backing array is no longer 'static' to this TU — it's
 *     internal but reachable through the public WarningStatus/WarningValue
 *     accessors only.  No code in or out of this module touches it
 *     directly.
 *
 * Behaviour preserved bit-for-bit:
 *   • WarningsSet() switch table (special-cased indexes for bitmaps,
 *     EXPANSION_x offset, EQ_LIGHTSx offset, EQ_REFRIG_DEFROST1 offset,
 *     EQ_HUMID_HEADx remap, WARN_AUX value-as-status convolution).
 *   • IsBoardWarning() composite check (8 board-related slots).
 *   • WarningsClearChk() snapshot semantics (used by legacy
 *     StorePostData.c to dedup outbound POSTs).
 *
 * This file must stay free of #ifdef CONSTELLATION_NOVA and any other
 * legacy-style conditional compilation.  It is Nova-native.
 */

#include <string.h>
#include <stdint.h>

#include "Settings.h"        /* legacy header — provides EQUIPMENT_IO etc.       */
#include "States.h"          /* legacy header — provides FM_PRELIM, FM_ALARM     */
#include "Warnings.h"        /* the API contract we implement                    */

/* WARNING storage — same shape as legacy.  Backing arrays are file-local
 * but accessible through the WarningStatus/WarningValue accessors. */
static WARNING s_warning[NUM_WARNINGS];
static WARNING s_warning_chk[NUM_WARNINGS];

/* Bitmap helper — set bit `eq_io` inside the .Value[] uint32 array,
 * spilling across elements at 32-bit boundaries.  Matches legacy. */
static void warnings_set_bitmap(WARNING_ITEMS index, uint32_t eq_io)
{
    const unsigned bits_per_elem = (unsigned)(sizeof(s_warning[0].Value[0]) * 8u);
    unsigned element = (unsigned)eq_io / bits_per_elem;
    unsigned bit     = (unsigned)eq_io % bits_per_elem;
    if (element < WARNING_VALUE_LEN) {
        s_warning[index].Value[element] |= (1u << bit);
    }
}

/* ─── Public API ────────────────────────────────────────────────────── */

int IsBoardWarning(void)
{
    if (   s_warning[WARN_NEWBOARD].Status     != 0
        || s_warning[WARN_BOARDREMOVED].Status != 0
        || s_warning[WARN_COMMERR].Status      != 0
        || s_warning[WARN_DEFAULTTEMP].Status  != 0
        || s_warning[WARN_BOARDNOTTEMP].Status != 0
        || s_warning[WARN_BOARDNOTHUMID].Status!= 0
        || s_warning[WARN_DEFTEMPDIS].Status   != 0
        || s_warning[WARN_DEFHUMDIS].Status    != 0)
    {
        return 1;
    }
    return 0;
}

void WarningsClear(void)
{
    memset(s_warning, 0, sizeof(s_warning));
}

void WarningsClearChk(void)
{
    memset(s_warning_chk, 0, sizeof(s_warning_chk));
}

/* WarningsSendToUI — Nova has no ASCII UI bridge; the proto WarningReport
 * is sent by NovaDataExc_Tick() with poll-based change detection.  This
 * symbol is kept only so legacy translation units that include
 * Warnings.h still link cleanly. */
void WarningsSendToUI(int ForceSend)
{
    (void)ForceSend;
    /* intentionally empty */
}

void WarningsSet(WARNING_ITEMS index, char status, uint32_t value, uint32_t eqIo)
{
    if ((unsigned)index >= NUM_WARNINGS) return;

    s_warning[index].Status = status;

    switch (index)
    {
        case WARN_SYSCONFIG_EQ:
        case WARN_NO_OUTPUT:
        case WARN_NEWBOARD:
        case WARN_COMMERR:
        case WARN_BOARDREMOVED:
            warnings_set_bitmap(index, eqIo);
            break;

        case WARN_EXPANSIONBOARD:
            warnings_set_bitmap(index, eqIo - EXPANSION_1);
            break;

        case WARN_LIGHTS:
            warnings_set_bitmap(index, eqIo - EQ_LIGHTS1);
            break;

        case WARN_REFRIG_STAGE:
            warnings_set_bitmap(index, eqIo);
            s_warning[index].Value[1] = value;
            break;

        case WARN_REFRIG_DEFROST:
            warnings_set_bitmap(index, eqIo - EQ_REFRIG_DEFROST1);
            s_warning[index].Value[1] = value;
            break;

        case WARN_AUX:
            /* Legacy convolution preserved: WARN_AUX uses `value` as the
             * status field rather than `status`.  Documented in legacy
             * Warnings.c with a TODO; preserving bit-for-bit compatibility
             * until the AUX failure path migrates Nova-side. */
            warnings_set_bitmap(index, eqIo);
            s_warning[index].Status = (char)value;
            break;

        case WARN_HUMIDIFIER:
            switch (eqIo) {
                case EQ_HUMID_HEAD1: warnings_set_bitmap(index, 0); break;
                case EQ_HUMID_HEAD2: warnings_set_bitmap(index, 1); break;
                case EQ_HUMID_HEAD3:
                default:             warnings_set_bitmap(index, 2); break;
            }
            s_warning[index].Value[1] = value;
            break;

        default:
            s_warning[index].Value[0] = value;
            break;
    }
}

char WarningStatus(WARNING_ITEMS index)
{
    if ((unsigned)index >= NUM_WARNINGS) return 0;
    return s_warning[index].Status;
}

void WarningValue(WARNING_ITEMS index, uint32_t *value)
{
    if (value == NULL) return;
    if ((unsigned)index >= NUM_WARNINGS) {
        for (int i = 0; i < WARNING_VALUE_LEN; ++i) value[i] = 0;
        return;
    }
    for (int i = 0; i < WARNING_VALUE_LEN; ++i) {
        value[i] = s_warning[index].Value[i];
    }
}

/*** End of file ***/

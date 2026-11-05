// Titik masuk dashboard: memuat semua modul fitur.
import "./nav.js";
import "./dashboard.js";
import "./rain.js";
import { initControl } from "./control.js";
import { initCalibrationTss } from "./calibration-tss.js";
import { initTssFactor } from "./tss-factor.js";
import { initTssHwWizard } from "./tss-hw-wizard.js";
import { initPhCalibration } from "./ph-calibration.js";
import { initMode } from "./mode.js";
import { initCountdown } from "./countdown.js";
import { initSchedule } from "./schedule.js";
import { initLayoutEditor } from "./layout-editor.js";

initControl();
initCalibrationTss();
initTssFactor();
initTssHwWizard();
initPhCalibration();
initMode();
initCountdown();
initSchedule();
initLayoutEditor();

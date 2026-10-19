// SPDX-License-Identifier: GPL-3.0-only
// CoralFlower's browser emulator: the computer keyboard, by KeyboardEvent.code (physical US-layout positions).
// The 27 keys as a tracker's piano: the lower ("white") keys on Z X C V B N M and Q W E R T Y U I O, the upper
// ("black") keys on the row above each (S D F H J and 2 3 4 6 7 9). kind: key (firmware key 0..26: MIDI 53 + k),
// btn (EMU_B_* label index), turn (role, detents), cycle (the knob the arrows turn).
globalThis.FM1 = {
  KEYMAP: [
    ["KeyZ", "key", 0, "Z"], ["KeyS", "key", 1, "S"], ["KeyX", "key", 2, "X"], ["KeyD", "key", 3, "D"],
    ["KeyC", "key", 4, "C"], ["KeyF", "key", 5, "F"], ["KeyV", "key", 6, "V"], ["KeyB", "key", 7, "B"],
    ["KeyH", "key", 8, "H"], ["KeyN", "key", 9, "N"], ["KeyJ", "key", 10, "J"], ["KeyM", "key", 11, "M"],
    ["KeyQ", "key", 12, "Q"], ["Digit2", "key", 13, "2"], ["KeyW", "key", 14, "W"], ["Digit3", "key", 15, "3"],
    ["KeyE", "key", 16, "E"], ["Digit4", "key", 17, "4"], ["KeyR", "key", 18, "R"], ["KeyT", "key", 19, "T"],
    ["Digit6", "key", 20, "6"], ["KeyY", "key", 21, "Y"], ["Digit7", "key", 22, "7"], ["KeyU", "key", 23, "U"],
    ["KeyI", "key", 24, "I"], ["Digit9", "key", 25, "9"], ["KeyO", "key", 26, "O"],
    ["F1", "btn", 0, "F1"], ["F2", "btn", 1, "F2"], ["F3", "btn", 2, "F3"], ["F4", "btn", 3, "F4"],
    ["F5", "btn", 4, "F5"], ["F6", "btn", 5, "F6"], ["Home", "btn", 6, "HOME"], ["F7", "btn", 6, "F7"],
    ["F8", "btn", 7, "F8"], ["F9", "btn", 8, "F9"], ["F10", "btn", 9, "F10"], ["Space", "btn", 10, "SPACE"],
    ["Enter", "btn", 11, "ENTER"], ["Minus", "btn", 12, "-"], ["Equal", "btn", 13, "="],
    ["BracketLeft", "turn", [2, -1], "["], ["BracketRight", "turn", [2, 1], "]"],
    ["Semicolon", "turn", [0, -1], ";"], ["Quote", "turn", [0, 1], "'"],
    ["Comma", "turn", [1, -1], ","], ["Period", "turn", [1, 1], "."],
    ["Tab", "cycle", 1, "TAB"], ["ArrowUp", "arrow", 1, "UP"], ["ArrowDown", "arrow", -1, "DOWN"],
    ["ArrowRight", "arrow", 1, "RIGHT"], ["ArrowLeft", "arrow", -1, "LEFT"],
  ].map(([code, kind, idx, cap]) => ({ code, kind, idx, cap })),
  // the printed labels (EMU_B_*) and what CoralFlower makes of them
  BTN: ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"],
  BTN_ROLE: ["EFFECTS", "CHORD", "ENVELOPE", "BASS", "SOUND", "GLOBAL", "HOME", "SAVE", "ARP", "SEQUENCER", "PLAY", "REC", "OCT−", "OCT+"],
  // EMU_E_* knobs and their roles
  ENC: ["SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4", "MASTER"],
  ENC_ROLE: ["BASS SOUND", "ALGORITHM", "CHORD SOUND", "K1", "K2", "K3", "K4", "VOLUME"],
  KEY_LABEL: ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", ""],
  noteName(k) {
    const N = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], m = 53 + k;
    return N[m % 12] + (Math.floor(m / 12) - 1);
  },
};

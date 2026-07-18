// Single source of truth for the board FQBN. `m5stack_cardputer` covers the
// Adv too (no ADV-specific board exists; M5Unified detects it at runtime).
// The board defaults assume 4MB flash; the Adv has 8MB, and without these
// options the 1.2MB app partition overflows.
export const FQBN =
  'm5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB';

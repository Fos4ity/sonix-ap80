#ifndef FWUPDATE_H
#define FWUPDATE_H

// The "Update firmware" card: from the internet (ota.h) or from the card
// (firmware.h), over a dimmed page.
//
//   choice      Via internet / From SD / Cancel
//   internet    checking -> the newest release, its notes and an Update
//               button, or why there is none -> the download with a progress
//               bar -> recovery, exactly as from the card
//   from SD     the file on the card, or a notice that there is none
//
// The screen stays on while the card is open.

void fwupdate_show(void);

// Beside the stock firmware (sonixpkg.h) the card updates Sonix itself from
// its GitHub releases instead: no firmware, no "From SD". This starts the
// quiet check made once per start, after Wi-Fi connects; the card only
// appears when a newer release is there. [update] auto_check=false stops it.
void fwupdate_autocheck_init(void);

#endif /* FWUPDATE_H */

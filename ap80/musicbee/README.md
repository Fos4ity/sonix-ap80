# MusicBee add-on: send tracks to Sonix over Wi-Fi

Optional. Adds "send to player" to MusicBee on Windows. It uploads the selected
tracks to Sonix's Wi-Fi transfer page, into `Music/<Album artist>/<Album>/` on the card.

- `SonixSend.cs` is a MusicBee plugin. To build it you also need
  `MusicBeeInterface.cs` from the official MusicBee plugin API (not included here,
  it is MusicBee's file). Put it next to `SonixSend.cs` and run `build.cmd`.
- `SonixSendExe.cs` is a standalone program for MusicBee's "Send to > external
  application" menu.

Both read the player's address from a one-line text file (see the comment at the
top of each source file). The default `http://192.168.1.100/` is only an example:
use the address Sonix shows on its Wi-Fi transfer page.

The menu texts are in Czech.

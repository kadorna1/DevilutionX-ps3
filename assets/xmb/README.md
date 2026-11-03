# PS3 XMB media

The packaging script copies only these three files to the root of the generated PS3 PKG beside `PARAM.SFO`:

- `ICON0.PNG`: PS3 static XMB icon (320×176).
- `PIC1.PNG`: PS3 XMB background (1920×1080).
- `SND0.AT3`: PS3 XMB selection sound. It is copied byte-for-byte, without decoding or re-encoding.

`LOGO0.PNG` is preserved in this source directory for reference but is unused by the PS3 package and is not included in it.

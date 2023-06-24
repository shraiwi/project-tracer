# Ideas!

This is just a simple document for me to jot down ideas regarding the project, TODOs, and implementations.

- Use 14430 LiFePO4 Batteries for the power source. Their voltage range is well within what the ESP32 can handle (3.6V-2.5V), and they have capacities of around 400mAh, which should let the ESP32 run for 2 days, approximately.
- ~~Dilemma! How can you get keys off of the device?! I was thinking of using NFC, but then i realized that's not very cross-platform compatible. WiFi is very power consuming. Maybe bluetooth?~~ WiFi was surprisingly easy to implement!
- ~~Thinking of using setInterval() instead of requestAnimationFrame() for the website to enforce a strict 30FPS data transfer because not all displays are 60Hz.~~
- Setup via SoftAP is now a thing
- Case IDs are defined as a base32 string encoding a 4-byte randomly-generated number with its trailing "=" removed (ex. `abc123`). Although they are case-insensitive, they should be displayed as lowercase-only so that it's easier for the user to type.
## Example Tracer Data:
```text
tek: 4c22bda759942e0758e47922ed4d6c3e
rpik: 1ab8a1141ecd1a6ee9f6a33b7296322a
aemk: e47c05afa4c51eeb286344f1aa634111
rpi: f603d5f00d002e8eba6cd65090e530ec
metadata: 40050000
aem: 29fbe29e
ble advertising payload: 02011a03036ffd17166ffdf603d5f00d002e8eba6cd65090e530ec29fbe29e
```
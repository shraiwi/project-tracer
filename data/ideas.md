# Ideas!

This is just a simple document for me to jot down ideas regarding the project, TODOs, and implementations.

- Use 14430 LiFePO4 Batteries for the power source. Their voltage range is well within what the ESP32 can handle (3.6V-2.5V), and they have capacities of around 400mAh, which should let the ESP32 run for 2 days, approximately.
- ~~Dilemma! How can you get keys off of the device?! I was thinking of using NFC, but then i realized that's not very cross-platform compatible. WiFi is very power consuming. Maybe bluetooth?~~ WiFi was surprisingly easy to implement!
- Thinking of using setInterval() instead of requestAnimationFrame() for the website to enforce a strict 30FPS data transfer because not all displays are 60Hz.
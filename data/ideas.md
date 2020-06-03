# Ideas!

This is just a simple document for me to jot down ideas regarding the project, and implementations.

- Use 14430 LiFePO4 Batteries for the power source. Their voltage range is well within what the ESP32 can handle (3.6V-2.5V), and they have capacities of around 400mAh, which should let the ESP32 run for 2 days, approximately.
- Dilemma! How can you get keys off of the device?! I was thinking of using NFC, but then i realized that's not very cross-platform compatible. WiFi is very power consuming. Maybe bluetooth?
# Project Tracer

An Open Source, ESP32-based contact tracing beacon!

## Intro

Well, here we are again.

This is an open-source contact tracing API designed from the ground-up based off of the Google x Apple standard (see it [**here**](https://www.apple.com/covid19/contacttracing)). It aims to provide a lower-cost option for contact tracing for those who do not have cellphones, or those whose cellphones don't support it. In the end, I plan on it being implemented into a device (keychain-sized) which could easily fit into someone's pocket.

## Project Log

### May 9th

- Switched to the ESP-IDF instead of the Arduino IDE.

- Coded basic BLE advertising framework

### May 10th

- Finished work on basic BLE Adapter (basic classes & record implementation)

- Added some cryptographic functions (RNG, SHA256 hash)

### May 11th

- Added some more cryptographic functions, (HKDF, AES128 encryption)

- Verified the functionality of the functions.



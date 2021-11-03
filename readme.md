![Project Tracer Logo](./media/logo.png)

Project Tracer is an open-source contact tracing implementation for microcontrollers. At its current stage, it is designed for the ESP32, a low-power microcontroller with WiFi and Bluetooth. However, the code is written to be portable and can easily be ported to almost any platform.

[Hackster.io project page](https://www.hackster.io/epicface2304/project-tracer-confidential-contact-tracing-for-the-masses-a6e2dc)

## Features
- **🔐 Completely privacy preserving**
    > Unlike other methods of contact tracing that use machine vision or GPS, no data will ever leave the device that can be used to identify the user. All of the information used to notify others of potential exposures is randomly generated and encrypted.
- **🔋 Long Battery Life**
    > Project Tracer provides an extremely low-maintenance method of contact tracing, as opposed to phones. While a user's phone may run out of battery after less than a day, Project Tracer's projected power consumption can keep the device running for almost 5 days! In fact, the average power consumption is so low that the device can be charged by a single solar cell!
- **👐 Simple Setup**
    > The setup of a tracer device is extremely simple. Just tap a button, connect to the hotspot, and configure the device!
- **📁 Header-only**
    > The code responsible for the contact tracing API is completely header-only and does not have any external dependencies outside of the C standard library and mbedTLS!
- **💲 Low Cost**
    > While the BOM cost for a single card in individidual quantities is high (about $13), bulk pricing can bring the price down to something as low as $5 per card.

## Demo
[![A demo of the Tracer API in action](http://img.youtube.com/vi/fehssvGHECE/0.jpg)](http://www.youtube.com/watch?v=fehssvGHECE "Tracer API Demo")

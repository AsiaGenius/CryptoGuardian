# CryptoGuardian Shield

This repository is exclusive to Ragnarok Online. Here are some useful links below.

## How to Install CryptoGuardian
Check out the complete documentation.

> **CryptoGuardian Docs:** http://docs.cryptoguardian.net


## How to Get Support
How to get Support?
You can get support by contacting someone with the **DevTeam** tag in the discord. This is the fastest way to get real-time support.


## How to Work?

Packet Transmission Schema:

```mermaid
graph LR
A[hexed.exe] -- Encrypted Packets --> B((ring.dll))
A[hexed.exe] -- license check --> C((Google Cloud))
B{ring.dll} --Validated--> A{hexed.exe}
C --> D{emulator}
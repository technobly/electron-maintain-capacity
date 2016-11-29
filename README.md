# electron-maintain-capacity

Particle Electron - Maintain a Minimum Battery Capacity App

- Designed for use with the 2000mAh LiPo battery that ships with the Electron.
- Electron will deep sleep if battery capacity falls below LOW_BATT_CAPACITY (default 20.0%)
- Electron sleeps long enough to charge up well past the LOW_BATT_CAPACITY but will power back on if above LOW_BATT_CAPACITY.
- Sleep duration will increase exponentially starting at 24 minutes, increasing to 51.2 hours max.
- Please read through the comments to understand logic.
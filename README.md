## To build

```bash
python -m esptool --chip esp32s3 --port COM3 --baud 115200 erase_flash 
python -m esptool --chip esp32s3 --port COM3 --baud 115200 write_flash -z 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

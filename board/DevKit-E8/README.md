# Alif E8 board layers

The layers and RTE configuration files are adapted from the installed
`AlifSemiconductor::Ensemble@2.2.1` pack's `Boards/DevKit-e8/Layers/M55_HP`
and `M55_HE` directories. The original vendor license is in `License.txt`.

The layer manifests select only the components needed by this bare-metal demo.
The pack's RTOS, NPU, camera, display and other peripheral applications are not
included. Startup, linker scripts and memory configuration come from the pack.
HP uses the pack's `Services:Retarget IO:STDOUT` implementation and UART4 at
115200 baud, 8 data bits, no parity, 1 stop bit, without flow control. HE only
executes an idle loop and does not initialize stdout.

The pack's MRAM layout places HP at `0x80200000` and HE at `0x80000000`, matching
the existing dual-core debug-stub configuration. Both images belong to each
target set; use `DevKit-E8@Release` for the normal demo.

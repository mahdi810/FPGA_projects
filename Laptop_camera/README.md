# Zynq HDMI Video Capture and Gigabit Ethernet Streaming

Real-time video capture over HDMI on a PYNQ-Z2, buffered into DDR by DMA, and
streamed to a PC over UDP by a bare-metal ARM application. No operating system
on the board.

**Result: 640×360 at ~43 fps over gigabit Ethernet, bare metal.**

> *(Add a short screen capture of the Python viewer here. A 15-second clip of the
> live feed is worth more than any description.)*

---

## What this does

A laptop mirrors its webcam to an HDMI output. The FPGA receives that HDMI
signal, decodes it to pixels, stores frames in DDR memory, and a bare-metal C
program running on the ARM core packetises and streams them back to the PC over
Ethernet, where a Python script reassembles and displays them.

The point of the project is the full-stack path: raw differential signalling at
one end, a live window on a PC at the other, with every layer in between built
explicitly.

```
Laptop (webcam via OBS)
   │  HDMI, TMDS serial @ 742.5 Mb/s per lane
   ▼
┌─────────────────────── PL (FPGA fabric) ───────────────────────┐
│  dvi2rgb            deserialise, TMDS decode, EDID over DDC    │
│      │  24-bit RGB + HSync/VSync/DE @ 74.25 MHz pixel clock    │
│      ▼                                                          │
│  Video In to AXI4-Stream    sync signals → TUSER/TLAST,        │
│      │                      async FIFO crosses to 100 MHz AXI   │
│      ▼                                                          │
│  AXI VDMA (S2MM)    burst writes into DDR, 3-buffer ring       │
└──────┬──────────────────────────────────────────────────────────┘
       │  AXI4 64-bit via S_AXI_HP0
       ▼
┌─────────────────────── PS (ARM Cortex-A9) ─────────────────────┐
│  DDR frame buffers @ 0x10000000, 3 × 2.7 MB                    │
│  Bare-metal C:  cache invalidate → downscale 2× → packetise    │
│  lwIP raw API (UDP) → hard GEM MAC → RGMII → PHY               │
└──────┬──────────────────────────────────────────────────────────┘
       │  UDP, ~1400 B payloads, 494 packets/frame
       ▼
   PC: Python + OpenCV — reassemble by frame_id/offset, display
```

---

## Measured results

| Metric | Value |
|---|---|
| Capture resolution | 1280×720 @ 60 Hz, RGB888 |
| Transmit resolution | 640×360 (2× decimation on the ARM core) |
| Frame rate delivered | ~43 fps |
| Network throughput | ~240 Mbit/s |
| Link speed | 1000 Mbit/s (auto-negotiated) |
| DDR frame buffers | 3 × 2.7 MB ring |
| Packets per frame | 494 |
| Software stack | bare metal, no OS |

Full 720p RGB is 166 MB/s (1.3 Gbit/s), which exceeds gigabit Ethernet before
any protocol overhead. Decimating by 2 in both axes brings this to 27 MB/s and
leaves comfortable headroom. The current bottleneck is the ARM core copying
every pixel twice — once to downscale, once into each lwIP buffer.

---

## Hardware and tools

- **Board:** TUL PYNQ-Z2 (Zynq-7020, XC7Z020-1CLG400C)
- **Tools:** Vivado 2023.1, Vitis 2023.1
- **IP:** Digilent `dvi2rgb` v2.0, Xilinx `v_vid_in_axi4s` v5.0, `axi_vdma` v6.3
- **PC side:** Python 3, NumPy, OpenCV

---

## Block design

| Block | Purpose |
|---|---|
| `processing_system7_0` | DDR controller, ARM cores, gigabit MAC. `M_AXI_GP0` for control, `S_AXI_HP0` (64-bit) for VDMA writes, `FCLK_CLK0` at 100 MHz, `FCLK_CLK1` at 200 MHz, `IRQ_F2P` |
| `dvi2rgb_0` | HDMI receiver. `kClkRange = 3` (sub-80 MHz, i.e. 720p), DDC ROM enabled advertising 1280×720 |
| `v_vid_in_axi4s_0` | RGB + syncs → AXI4-Stream. Independent clock mode, 1024-deep input FIFO |
| `axi_vdma_0` | Write channel only, 3 frame stores, 64-bit memory map, 16-beat bursts, frame sync from `s2mm tuser` |
| `smartconnect_0` | VDMA `M_AXI_S2MM` → `S_AXI_HP0` (data path) |
| `smartconnect_1` | `M_AXI_GP0` → VDMA `S_AXI_LITE` (control path) |
| `proc_sys_reset_0/1` | One per clock domain — 100 MHz AXI, and the recovered pixel clock |
| `xlconstant_0/1` | Tie `axis_enable` high; drive HDMI hot-plug detect high |

**Clock domains.** Three, and keeping them straight is most of the design:

1. **Pixel clock** — recovered from the HDMI cable by `dvi2rgb`'s MMCM. 74.25 MHz
   for 720p60. Its frequency is set by the source, not by the board.
2. **AXI clock** — 100 MHz from the PS PLL. Everything AXI runs here.
3. **200 MHz reference** — required by `IDELAYCTRL` inside `dvi2rgb` to calibrate
   the input delay lines on each TMDS lane.

The crossing between (1) and (2) happens in one place only: the async FIFO inside
`v_vid_in_axi4s`. The XDC declares the two domains asynchronous so the timing
engine doesn't try to relate them.

---

## Repository layout

```
Laptop_camera/
├── README.md
├── src/
│   ├── hdmi_in.xdc          pin constraints and clock definitions
│   ├── video_udp.c          VDMA setup, downscale, UDP packetiser
│   └── video_rx.py          PC-side receiver and viewer
└── docs/
    └── block_diagram.pdf
```

---

## Reproducing it

**1. Vivado**

```tcl
# Add the Digilent IP library to the catalogue first:
#   git clone https://github.com/Digilent/vivado-library
#   Settings → IP → Repository → add that folder
source ./build_project.tcl
```

Then Generate Bitstream, and File → Export → Export Hardware (include bitstream).

**2. Vitis**

- Create a platform from the exported `.xsa` (standalone, `ps7_cortexa9_0`)
- Add the `lwip213` library in **Modify BSP Settings** → Supported Libraries
- Create an application from the **lwIP Echo Server** template
- Add `video_udp.c` to `src/`, and in `main.c` replace `start_application()` with
  `video_init()` and `transfer_data()` with `video_send_frame(echo_netif)`

**3. Network**

Set the PC's Ethernet adapter to a static `192.168.1.100 / 255.255.255.0`. The
board defaults to `192.168.1.10`. Connect directly, no switch needed.

**4. Video source**

Connect HDMI from the laptop to the board's **HDMI IN**. Windows should detect a
second display — this confirms HPD and the EDID are working. Set it to 1280×720.
In OBS, add the webcam as a source, set the canvas to 1280×720, then right-click
the preview → Open Preview Projector → the second display.

**5. Run**

```bash
python src/video_rx.py
```

---

## Things that took the longest

Documented because the debugging was most of the work.

**`pRst_n` left unconnected on `dvi2rgb`.** Vivado ties unconnected inputs to 0.
Since `pRst_n` is active low, the entire pixel-clock half of the decoder was held
in reset permanently. Everything else worked — the EDID is served by a separate
I²C ROM in the other reset domain, so Windows happily detected a monitor and sent
video that the FPGA could never decode. The design built cleanly with no errors.
The lesson: an unconnected active-low reset is silently fatal, and "the build
passed" means nothing.

**Cache coherency.** The PL writes DDR through `S_AXI_HP0`, which bypasses the
ARM caches entirely. Without `Xil_DCacheInvalidateRange` before each read, the CPU
returns stale cached lines forever — the capture looks completely dead when it is
in fact working perfectly.

**`HoriSizeInput` is in bytes, not pixels.** Passing 1280 instead of 3840 captures
one third of each line.

**Address segments defaulting to excluded.** The VDMA is an AXI master and has its
own address space, separate from the CPU's. Both need populating: the CPU needs
the VDMA's registers mapped, and the VDMA needs DDR mapped. Only the first is
obvious.

**Two SmartConnects wired backwards.** Control path routed to the memory port and
vice versa. The symptom was an address segment that refused to stay assigned,
which is a long way from the cause.

---

## Known limitations

- **Fixed resolution.** The EDID advertises 1280×720 only, and the geometry is
  compiled in. A Video Timing Controller in detect mode would allow the software
  to discover the resolution at runtime.
- **Downscaling is nearest-neighbour** and aliases badly on fine detail. Averaging
  2×2 blocks would look far better at roughly 4× the CPU cost.
- **The ARM core is the bottleneck.** Two full-frame copies per frame. Moving the
  downscale and packetisation into the fabric is the obvious next step and would
  remove the CPU from the data path entirely.
- **No flow control.** UDP with no retransmission — packets lost under load leave
  stale bands in the image. Acceptable for a live view, not for a recording.
- **No HDMI output path.** Adding `AXI4-Stream to Video Out` + `rgb2dvi` would
  allow passthrough to a monitor, which is by far the best debugging tool for
  this kind of design.

---

## Next steps

1. Hardware packetiser: move frame-to-UDP into the PL and compare throughput,
   latency and CPU load against this software baseline
2. HDMI output passthrough for visual verification
3. An HLS processing block inserted into the stream (grayscale, then Sobel)
4. Runtime resolution detection via the Video Timing Controller

---

## Licence

MIT. See [LICENSE](../LICENSE).

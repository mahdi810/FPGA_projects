# HDMI In → Process → HDMI Out on Zynq

A reference for the components involved, and an honest assessment of what the project
teaches you about digital image processing.

Target hardware assumed: PYNQ-Z2 (Zynq-7020), Vivado + Vitis.

---

## Part 1 — The data path

### The shape of the problem

Video arrives on HDMI as a serial stream, becomes pixels, gets modified, becomes a serial
stream again, and leaves on HDMI. Everything in the block design exists to serve one of
those four transitions.

```
HDMI in → decode → [ PROCESSING ] → encode → HDMI out
```

The interesting design decision is what sits in the middle, and there are two fundamentally
different answers.

---

### Architecture A — Streaming (processing lives in the fabric)

Pixels flow continuously from input to output. Nothing is ever stored as a whole frame.
Your processing block sits inline and transforms pixels as they pass.

```
dvi2rgb → Video In to AXI4-Stream → [your IP] → AXI4-Stream to Video Out → rgb2dvi
```

**Latency:** a few lines of video, typically under 100 microseconds.
**Memory:** none, or a handful of line buffers in BRAM.
**Constraint:** you must consume and produce one pixel per clock, forever. You cannot
pause to think.

This is what real-time video hardware actually looks like. It's also the version that
teaches you the most about FPGA design, because the "one pixel per clock, no exceptions"
constraint forces you to think in pipelines rather than loops.

**Suitable for:** point operations, convolutions, colour space conversion, thresholding,
gamma, edge detection, any filter with a small local neighbourhood.

**Not suitable for:** anything needing the whole frame before it can decide — histogram
equalisation, frame differencing, geometric warps, rotation.

---

### Architecture B — Frame buffered (processing goes through DDR)

Pixels are written into DDR as complete frames, processed there, and read back out.

```
dvi2rgb → Video In to AXI4-Stream → VDMA (write) → DDR
                                                     ↓
                                          [CPU or accelerator]
                                                     ↓
DDR → VDMA (read) → AXI4-Stream to Video Out → rgb2dvi
```

**Latency:** at least one frame, so 16–33 ms. Often two or three.
**Memory:** several megabytes of DDR, plus real bandwidth pressure.
**Freedom:** you can do anything, in any order, taking as long as you like.

**Suitable for:** algorithms needing global information or multiple passes, anything
you want to prototype in C first, anything involving frame-to-frame comparison.

---

### Architecture C — Hybrid (what most real systems do)

Capture to DDR, run a hardware accelerator that reads from DDR and writes back to DDR,
display from DDR. The CPU only orchestrates — it configures the DMA engines and never
touches pixel data.

This is the architecture worth aiming for eventually. It combines the flexibility of
frame buffering with the throughput of hardware processing.

---

## Part 2 — Component list

### Input side (required in all architectures)

| Component | Role | Notes |
|---|---|---|
| `dvi2rgb` (Digilent) | HDMI receiver | Deserialises TMDS, decodes to 24-bit RGB, recovers the pixel clock, serves the EDID over DDC |
| `xlconstant` → HPD pin | Hot-plug detect | Must be driven high or the source never sends video |
| `Video In to AXI4-Stream` | Format conversion | Turns RGB + HSync/VSync/DE into an AXI4-Stream with `TUSER` = start of frame, `TLAST` = end of line. Also crosses from the pixel clock domain to the AXI clock domain |
| `Video Timing Controller` (detect mode) | Resolution discovery | Optional but useful — measures the incoming timing so software can learn the resolution rather than assuming it |

### Output side (required in all architectures)

| Component | Role | Notes |
|---|---|---|
| `AXI4-Stream to Video Out` | Format conversion | The mirror of the input converter — regenerates sync signals from the stream |
| `Video Timing Controller` (generate mode) | Timing generation | Produces HSync/VSync/blanking for the output resolution |
| `rgb2dvi` (Digilent) | HDMI transmitter | Encodes RGB back to TMDS and serialises it |
| Clock source for the output pixel clock | Pixel clock | Either reuse the recovered input clock (passthrough only), or generate it with a Clocking Wizard / Dynamic Clock IP if the output resolution differs from the input |

### Processing options for the middle

| Component | Role | When to use |
|---|---|---|
| **Custom HLS IP** | Your algorithm | The main event. Write C/C++ with `hls::stream` interfaces, let Vitis HLS build the pipeline. This is where you learn |
| **Custom RTL** | Your algorithm, the hard way | Full control, much slower to develop. Worth doing once to understand what HLS generates |
| `Video Processing Subsystem` (Xilinx) | Scaling, colour conversion, deinterlacing | Ready-made, well tested. Good for the plumbing around your own block |
| `AXI VDMA` ×2 | Frame buffering | One write channel for capture, one read channel for display. Genlock keeps them from colliding |
| PS ARM cores | Software processing | Fine for prototyping, far too slow for real-time at any real resolution |

### Infrastructure (needed regardless)

| Component | Role |
|---|---|
| `ZYNQ7 Processing System` | DDR controller, CPU, Ethernet, and the AXI ports between PS and PL |
| `Processor System Reset` ×2–3 | One per clock domain. Resets must be released synchronously with the domain they reset |
| `AXI SmartConnect` ×2 | Protocol conversion (PS speaks AXI3, your IP speaks AXI4/AXI4-Lite), address decoding, arbitration |
| `xlconcat` | Bundles multiple interrupt lines onto `IRQ_F2P` |

---

## Part 3 — Clock domains

This is the part that causes the most confusion, so it's worth stating plainly.

A passthrough design has **three** unrelated clocks:

1. **Input pixel clock** — recovered from the HDMI cable. Its frequency is decided by the
   source device. You do not control it and it drifts.
2. **AXI clock** — typically 100–150 MHz, generated by the PS PLL. Everything AXI runs here.
3. **Output pixel clock** — whatever your output resolution requires.

In a pure passthrough at identical resolution you can reuse clock 1 for clock 3. As soon as
you scale, change resolution, or buffer through DDR, they must be separate.

Every boundary between these domains needs an asynchronous FIFO, which is why both video
converter blocks have an "Independent Clocks" option. Enabling it is not optional in any
real design.

---

## Part 4 — Bandwidth reality check

Worth internalising before designing anything:

| Format | Pixel rate | Raw data rate |
|---|---|---|
| 640×480 @60 | 25.2 MHz | 55 MB/s |
| 1280×720 @60 | 74.25 MHz | 166 MB/s |
| 1920×1080 @60 | 148.5 MHz | 373 MB/s |
| 3840×2160 @60 | 594 MHz | 1.5 GB/s |

A Zynq-7020's DDR3 delivers roughly 1000–1200 MB/s of usable bandwidth in practice.

A frame-buffered 1080p60 design needs 373 MB/s in **plus** 373 MB/s out — about
750 MB/s, most of your budget, before your processing block reads or writes anything.
This is why streaming architectures matter: they cost zero bandwidth.

---

## Part 5 — How useful is this for image processing experience?

### What it genuinely teaches you

**Video timing as a first-class concept.** Blanking intervals, sync polarity, pixel clocks,
the difference between active and total resolution. Every video system on earth uses these,
whether it's a camera sensor, MIPI, SDI, or DisplayPort. Learning them on HDMI transfers
directly.

**Streaming dataflow thinking.** The single most valuable habit this project builds. Software
image processing loads a frame into an array and indexes it freely. Hardware processing sees
pixels arrive in raster order, one per clock, and cannot go back. Every algorithm has to be
restructured around that. Once you can think this way you can implement things in hardware
that you previously only knew as software.

**Line buffers and windowing.** The moment you attempt a 3×3 filter you discover you need
three rows available simultaneously, but pixels arrive one row at a time. Building a line
buffer that shifts a 3×3 window across the image is the single most important structure in
hardware image processing — Sobel, Gaussian blur, median, erosion, dilation, Harris corners
all use it.

**Memory bandwidth as a design constraint.** In software, memory is free and infinite. Here
you count bytes per second and discover you don't have enough. This changes how you think
about algorithms permanently.

**Clock domain crossing.** Genuinely important and genuinely subtle. Metastability, async
FIFOs, why a signal crossing domains needs synchronisers.

**Timing closure and pipelining.** Adding a stage to hit a frequency target teaches you what
"critical path" really means.

**Hardware/software partitioning.** Deciding what the ARM core does versus what the fabric
does is the central skill in SoC design.

### What it does not teach you

Be clear-eyed about this:

- **Modern computer vision.** Nothing here touches deep learning, feature descriptors,
  camera calibration, or 3D reconstruction. Those are algorithm domains, mostly learned in
  Python.
- **Algorithm design.** You'll implement algorithms someone else invented. That's the correct
  order to learn in, but don't confuse implementation skill with algorithmic skill.
- **Production video pipelines.** Real systems use MIPI CSI sensors, ISP pipelines
  (demosaicing, auto-exposure, lens correction, tone mapping), and compression. HDMI input
  skips all of it.

### Where this experience is actually valued

FPGA video processing remains a real, if specialised, field:

- **Machine vision / industrial inspection** — high frame rate, low latency, deterministic.
  A strong FPGA niche.
- **Medical imaging** — endoscopy, ultrasound, surgical displays. Latency and reliability
  requirements that GPUs struggle with.
- **Broadcast and professional video** — SDI infrastructure, real-time effects, format
  conversion. Heavily FPGA-based.
- **Automotive ADAS** — though increasingly moving to dedicated SoCs.
- **Defence and aerospace** — sensor fusion, targeting, imaging. FPGAs dominate.
- **Scientific instrumentation** — high-speed cameras, particle detectors.

Honest caveat: a lot of general-purpose vision work has moved to GPUs and embedded SoCs
with hardware accelerators. FPGA video is not the mainstream path into computer vision.
It *is* a strong path into embedded hardware engineering, and it's differentiating —
far fewer engineers can do it, and the ones who can are hard to replace.

### The strongest argument for doing it

The skills transfer sideways more than they transfer forwards. Someone who has built a
working video pipeline on a Zynq can build a radar pipeline, an audio DSP chain, a
high-speed data acquisition system, or an ML inference accelerator. The specific knowledge
is about video; the general knowledge is about moving high-rate data through hardware
under real-time constraints, and that is one of the most transferable skills in embedded
engineering.

---

## Part 6 — Suggested progression

Each step adds exactly one new concept. Don't skip.

| # | Project | New concept introduced |
|---|---|---|
| 1 | HDMI in → HDMI out passthrough | Video timing, TMDS, EDID, clocking. No processing at all |
| 2 | Colour inversion (`255 - pixel`) | Inserting an IP into the stream. Purely combinational, no memory |
| 3 | RGB → grayscale | Fixed-point arithmetic in hardware |
| 4 | Brightness/contrast via lookup table | BRAM as a LUT, runtime parameters over AXI4-Lite |
| 5 | Threshold with software-set level | Software controlling hardware through registers |
| 6 | 3×3 blur | **Line buffers and window generation** — the key structural lesson |
| 7 | Sobel edge detection | Multiple parallel kernels, magnitude computation |
| 8 | Median filter | Sorting networks — combinational algorithms that have no software analogue |
| 9 | Capture to DDR, display from DDR | VDMA, genlock, frame buffer management |
| 10 | Frame differencing / motion detection | Multi-frame algorithms, DDR bandwidth pressure |
| 11 | Histogram + equalisation | Two-pass algorithms, statistics gathered in hardware |
| 12 | Corner detection (Harris/FAST) | Real feature extraction, deep pipelines |
| 13 | Scaling / rotation | Non-raster memory access patterns, interpolation |

Steps 1–2 are most of the difficulty in getting started. Steps 6–8 are where the real
learning is. Steps 10+ are where it starts resembling professional work.

---

## Part 7 — Practical advice

**Write the processing block in Vitis HLS, not RTL.** You'll iterate ten times faster, and
the generated pipelines are usually as good as hand-written. Learn RTL by inspecting what
HLS produces. Interfaces should be `hls::stream<ap_axiu<24,1,1,1>>` to match AXI4-Stream
video, with `TUSER` carrying start-of-frame and `TLAST` carrying end-of-line.

**Get passthrough working before anything else.** A design that displays the input unchanged
proves your clocking, EDID, TMDS decode, and output encoding all work. Every subsequent bug
is then narrowed to your processing block.

**Add a Video Test Pattern Generator.** Switching between "real HDMI input" and "known
synthetic pattern" with a register write isolates input problems from processing problems
instantly.

**Instrument with an ILA on the AXI4-Stream.** Watching `TVALID`, `TREADY`, `TUSER`, `TLAST`
tells you immediately whether pixels are flowing and where frames begin. Most "black screen"
bugs are visible in five seconds this way.

**Respect backpressure.** If your block deasserts `TREADY` even briefly, pixels back up into
the input FIFO and eventually overflow — the picture tears or drops frames. A streaming video
block must accept a pixel every single clock cycle.

**Handle resolution changes.** Real sources change resolution on the fly. Detect it with the
Video Timing Controller and reconfigure rather than assuming.

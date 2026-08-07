# Notes: HDMI in, process, HDMI out on the PYNQ-Z2

Working notes I put together while building the capture side of this project, so
I'd have somewhere to look when I extend it to do actual processing. Everything
here assumes PYNQ-Z2 and Vivado/Vitis 2023.1, since that's what I have.

## The basic idea

Video comes in on HDMI as a serial stream, gets turned into pixels, gets
modified, gets turned back into a serial stream, goes out on HDMI. Four
transitions. Every block in the design exists to serve one of them.

```
HDMI in -> decode -> [ processing ] -> encode -> HDMI out
```

The only interesting decision is what goes in the middle, and there are really
two answers.

## Option A: keep it streaming

Pixels flow straight through. Nothing is ever stored as a whole frame. The
processing block sits inline and modifies pixels as they go past.

```
dvi2rgb -> Video In to AXI4-Stream -> [my IP] -> AXI4-Stream to Video Out -> rgb2dvi
```

Latency is a few lines of video, so well under a millisecond. Memory cost is
zero, or a few line buffers in BRAM. The catch is that you have to accept and
produce one pixel every clock cycle forever. You can't stall to think about it.

That constraint is the whole reason this approach is worth doing. It forces you
to build pipelines instead of loops, which is the actual difference between
writing software and writing hardware.

Works for: point operations, small convolutions, colour conversion,
thresholding, gamma, edge detection.

Doesn't work for: anything that needs the whole frame before it can decide.
Histogram equalisation, frame differencing, rotation, geometric warps.

## Option B: go through DDR

Write complete frames to memory, process them there, read them back out.

```
dvi2rgb -> Video In to AXI4-Stream -> VDMA write -> DDR
                                                      |
                                            CPU or accelerator
                                                      |
DDR -> VDMA read -> AXI4-Stream to Video Out -> rgb2dvi
```

Costs at least one frame of latency, usually two or three. Eats megabytes of DDR
and real bandwidth. In exchange you can do anything, in any order, taking as
long as you want.

This is what I built for the Ethernet streaming version, minus the read side.

## Option C: both

Capture to DDR, run an accelerator that reads from DDR and writes back, display
from DDR. The CPU only sets up the DMA engines and never touches a pixel.

This is what real systems do and it's what I'm aiming at eventually.

## Blocks I need

Input side, always:

- `dvi2rgb` (Digilent). Deserialises TMDS, decodes to 24-bit RGB, recovers the
  pixel clock, and serves the EDID over the DDC pins.
- Something driving HPD high. I used an `xlconstant`. Without it the source
  never even looks for a monitor.
- `Video In to AXI4-Stream`. Turns RGB plus HSync/VSync/DE into a stream with
  TUSER marking start of frame and TLAST marking end of line. Also does the
  clock domain crossing.
- `Video Timing Controller` in detect mode. Optional. Measures the incoming
  timing so software can find out the resolution instead of assuming it. I
  hardcoded 720p instead, which I'll regret later.

Output side:

- `AXI4-Stream to Video Out`. Mirror image of the input converter.
- `Video Timing Controller` in generate mode, for the output syncs.
- `rgb2dvi` (Digilent). Encodes back to TMDS.
- A pixel clock for the output. Reuse the recovered input clock if it's a
  straight passthrough, otherwise generate it with a Clocking Wizard.

The middle:

- Custom HLS IP is the main option. Write C with `hls::stream` interfaces and
  let Vitis HLS build the pipeline.
- Custom RTL if I want to understand what HLS is doing. Slower to write.
- Xilinx `Video Processing Subsystem` for scaling and colour conversion. Not the
  interesting part, but saves time.
- Two VDMAs if I go the frame-buffered route.

Infrastructure, regardless:

- Zynq PS, two or three `Processor System Reset` blocks (one per clock domain),
  two SmartConnects, `xlconcat` for interrupts.

## Clock domains

This is the part that confused me longest, so writing it down.

A passthrough design has three unrelated clocks:

1. Input pixel clock, recovered from the cable. The source decides its
   frequency. I don't control it and it drifts.
2. AXI clock, 100 MHz from the PS PLL.
3. Output pixel clock, whatever the output resolution needs.

For pure passthrough at the same resolution you can reuse clock 1 as clock 3.
The moment you scale, change resolution, or go through DDR, they have to be
separate.

Every boundary between them needs an async FIFO. That's why both video converter
blocks have an "Independent Clocks" option, and why leaving it off produces a
design that builds fine and outputs garbage.

## Bandwidth

Worth knowing before designing anything:

| Format | Pixel clock | Data rate |
|---|---|---|
| 640x480 @60 | 25.2 MHz | 55 MB/s |
| 1280x720 @60 | 74.25 MHz | 166 MB/s |
| 1920x1080 @60 | 148.5 MHz | 373 MB/s |
| 3840x2160 @60 | 594 MHz | 1.5 GB/s |

The Zynq-7020's DDR3 gives roughly 1000 to 1200 MB/s in practice.

So a frame-buffered 1080p60 design needs 373 in plus 373 out, about 750 MB/s,
which is most of the budget before the processing block reads or writes
anything. Streaming costs nothing, which is the real argument for it.

## Order I'm planning to build things

Each step adds one new idea. Skipping ahead didn't work for me.

1. Passthrough, no processing at all. Proves timing, TMDS, EDID, clocking.
2. Colour inversion, `255 - pixel`. Just proves I can insert a block.
3. RGB to grayscale. Fixed point arithmetic.
4. Brightness and contrast via a lookup table in BRAM, with the level set from
   software over AXI4-Lite.
5. 3x3 blur. This is the one that matters. You need three rows at once but
   pixels arrive one row at a time, so you have to build a line buffer and slide
   a window across. Sobel, median, erosion, dilation and Harris corners all use
   the same structure, so once this works the rest are variations.
6. Sobel edge detection.
7. Median filter. Sorting networks, which have no software equivalent.
8. Capture to DDR and display from DDR. VDMA on both sides, genlock.
9. Frame differencing for motion detection.
10. Histogram equalisation. Two passes.
11. Corner detection.
12. Scaling and rotation. Non-raster memory access.

Steps 1 and 2 are most of the difficulty in getting started. Step 5 is where the
actual learning is.

## Practical stuff I learned the hard way

Write the processing block in HLS, not RTL, at least at first. Iteration speed
matters more than optimal output while you're still figuring out what you're
building. Interfaces should be `hls::stream<ap_axiu<24,1,1,1>>` to match
AXI4-Stream video, with TUSER as start of frame and TLAST as end of line.

Get passthrough working before anything else. If the input appears unchanged on
a monitor, then clocking, EDID, TMDS decode and output encoding are all proven,
and every bug after that is in your own block. I skipped this and spent a long
time chasing a problem that turned out to be an unconnected reset pin on
dvi2rgb, which a working passthrough would have caught immediately.

Add a Video Test Pattern Generator early. Being able to switch between real HDMI
and a known synthetic pattern with a register write separates input problems
from processing problems in seconds.

Put an ILA on the AXI4-Stream. Watching TVALID, TREADY, TUSER and TLAST tells
you straight away whether pixels are moving and where frames start. Most black
screen problems are obvious this way.

Respect backpressure. If your block deasserts TREADY even briefly, pixels back
up into the input FIFO and eventually overflow. A streaming block has to take a
pixel every cycle.

Handle resolution changes eventually. Real sources change resolution on the fly.
Detecting it with the VTC is better than assuming, which is what I currently do.

## Why I think this is worth doing

Mostly because of the way it forces you to think. In software you load a frame
into an array and index it wherever you like. Here, pixels arrive one per clock
in raster order and you can never go back. Restructuring an algorithm around
that is the actual skill, and it applies to anything with high rate data and
real time constraints, not just video.

The things it does not teach: modern computer vision, algorithm design, or
anything about real camera pipelines like demosaicing, auto exposure or lens
correction. HDMI input skips all of that.

Where it's useful: machine vision and industrial inspection, medical imaging,
broadcast, some automotive, defence, scientific instrumentation. It's a
specialised field rather than the mainstream route into vision work, most of
which has moved to GPUs. But few people can do it, which is the point.

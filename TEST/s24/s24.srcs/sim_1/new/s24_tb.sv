//=====================================================================
// s24 testbench - the same DUT at three levels of SystemVerilog
//
//   TIER 1  direct translation of your VHDL         ~ marginal gain
//   TIER 2  interface + clocking block + SVA        ~ the real win
//   TIER 3  class-based, randomised, coverage       ~ scales, overkill here
//
// Read them in order. The interesting jump is 1 -> 2.
//
// Tooling note: Tier 1 and most of Tier 2 run on free simulators.
// Tier 3 (classes, randomize, covergroup) effectively needs a commercial
// simulator - Questa, VCS or Xcelium. Use your university licences while
// you still have them.
//=====================================================================


//=====================================================================
// TIER 1 - direct translation
//
// Compare to the VHDL. Shorter, yes: no COMPONENT block, no procedure
// declarations section, terser syntax. But structurally identical, and
// it is no more correct. If this were the whole story, switching
// languages would not be worth your time.
//=====================================================================
module s24_tb_tier1;

    localparam time CLK_PERIOD = 10ns;

    logic clk = 0, reset = 1, start = 0, y_out;
    int   errors = 0;

    s24 uut (.clk(clk), .reset(reset), .start(start), .y_out(y_out));

    always #(CLK_PERIOD/2) clk = ~clk;

    // Concise check task; same idea as the VHDL procedure.
    task automatic check(bit cond, string msg);
        if (!cond) begin
            $error("CHECK FAILED: %s", msg);
            errors++;
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        reset <= 0;
        @(posedge clk);
        check(y_out === 0, "y_out must be low after reset");

        start <= 1;
        @(posedge clk);
        start <= 0;

        // fork/join_any gives a clean bounded wait - nicer than VHDL's
        // "wait until ... for ...", and this is a genuine small win.
        fork
            begin wait (y_out === 1); end
            begin repeat (80) @(posedge clk); end
        join_any
        check(y_out === 1, "y_out did not assert within 80 cycles");

        if (errors == 0) $display("=== TIER1 PASSED ===");
        else             $fatal(1, "=== TIER1 FAILED: %0d errors ===", errors);
        $finish;
    end

    initial begin
        #50us;
        $fatal(1, "WATCHDOG timeout");
    end

endmodule


//=====================================================================
// TIER 2 - where SystemVerilog starts genuinely paying off
//
// Two features do the work:
//
//   clocking block - solves the "drive a fraction after the edge"
//     problem declaratively. The output skew is part of the contract,
//     so you cannot accidentally drive on the sampling edge. This
//     replaces the drive_after_edge procedure entirely.
//
//   concurrent assertions (SVA) - temporal properties checked on every
//     clock, for the whole simulation, independent of which test is
//     running. This is the step change. Your procedural checks only
//     fire where you wrote them; these fire everywhere, and they catch
//     violations in scenarios you never thought to write.
//=====================================================================

interface s24_if (input logic clk);
    logic reset, start, y_out;

    clocking cb @(posedge clk);
        // #1step: sample inputs immediately before the edge (correct
        //         race-free sampling).
        // #2ns:   drive outputs 2ns after the edge, so transitions are
        //         always clear of the sampling point.
        default input #1step output #2ns;
        output reset, start;
        input  y_out;
    endclocking

    modport tb (clocking cb, output reset, output start, input y_out);
endinterface


module s24_tb_tier2;

    localparam time CLK_PERIOD = 10ns;
    localparam int  MAX_LATENCY = 80;

    logic clk = 0;
    always #(CLK_PERIOD/2) clk = ~clk;

    s24_if vif (.clk(clk));

    s24 uut (
        .clk   (clk),
        .reset (vif.reset),
        .start (vif.start),
        .y_out (vif.y_out)
    );

    //-----------------------------------------------------------------
    // Properties. These run for the entire simulation.
    //
    // Note what you get for free: writing the specification as
    // properties forces you to state precisely what the DUT must do -
    // and that act alone finds specification ambiguities. It is the
    // most underrated benefit of assertion-based verification.
    //-----------------------------------------------------------------

    // Nothing may come out while reset is asserted.
    property p_reset_clears;
        @(posedge clk) vif.reset |-> !vif.y_out;
    endproperty
    a_reset_clears : assert property (p_reset_clears)
        else $error("y_out asserted while reset high");

    // Every start must be answered within MAX_LATENCY cycles.
    property p_start_causes_output;
        @(posedge clk) disable iff (vif.reset)
            $rose(vif.start) |-> ##[1:MAX_LATENCY] vif.y_out;
    endproperty
    a_start_causes_output : assert property (p_start_causes_output)
        else $error("y_out did not follow start within %0d cycles", MAX_LATENCY);

    // y_out is a single-cycle pulse, not a level.
    property p_single_cycle_pulse;
        @(posedge clk) disable iff (vif.reset)
            $rose(vif.y_out) |=> !vif.y_out;
    endproperty
    a_single_cycle_pulse : assert property (p_single_cycle_pulse)
        else $error("y_out held for more than one cycle");

    // No spontaneous output: y_out implies a start happened earlier.
    property p_no_spurious_output;
        @(posedge clk) disable iff (vif.reset)
            $rose(vif.y_out) |-> $past(vif.start, 1, , @(posedge clk)) or 1;
    endproperty
    // (Left deliberately loose - tighten once you fix the exact latency.)

    // Coverage: did we actually exercise the interesting sequence?
    // An assertion that never fires because the stimulus never reached
    // it is worthless. cover property tells you the difference.
    c_start_to_output : cover property (
        @(posedge clk) $rose(vif.start) ##[1:MAX_LATENCY] vif.y_out
    );
    c_reset_mid_flight : cover property (
        @(posedge clk) $rose(vif.start) ##[1:20] vif.reset
    );

    //-----------------------------------------------------------------
    // Stimulus. Notice how little checking code is left - the
    // assertions above absorbed it. The stimulus job is now only to
    // reach interesting states, not to verify them.
    //-----------------------------------------------------------------
    task automatic do_reset();
        vif.cb.reset <= 1;
        vif.cb.start <= 0;
        repeat (4) @(vif.cb);
        vif.cb.reset <= 0;
        @(vif.cb);
    endtask

    task automatic pulse_start();
        vif.cb.start <= 1;
        @(vif.cb);
        vif.cb.start <= 0;
    endtask

    initial begin
        do_reset();

        // T1: basic operation
        pulse_start();
        repeat (MAX_LATENCY + 2) @(vif.cb);

        // T2: reset mid-flight
        pulse_start();
        repeat (5) @(vif.cb);
        vif.cb.reset <= 1;
        repeat (2) @(vif.cb);
        vif.cb.reset <= 0;

        // T3: back to back
        do_reset();
        pulse_start();
        repeat (MAX_LATENCY + 2) @(vif.cb);
        pulse_start();
        repeat (MAX_LATENCY + 2) @(vif.cb);

        // T4: spurious start while running
        do_reset();
        pulse_start();
        repeat (3) @(vif.cb);
        pulse_start();
        repeat (MAX_LATENCY + 2) @(vif.cb);

        $display("=== TIER2 finished - check assertion report ===");
        $finish;
    end

    initial begin
        #50us;
        $fatal(1, "WATCHDOG timeout");
    end

endmodule


//=====================================================================
// TIER 3 - constrained-random with functional coverage
//
// For a four-signal DUT this is overkill and you should not write it.
// It is here so you can see the shape, because this is the structure
// that scales to real designs and it is the on-ramp to UVM.
//
// The conceptual shift: you stop writing individual test cases and
// start describing the *space* of legal stimulus, then let the tool
// explore it while coverage tells you what has actually been reached.
// On a complex DUT this finds bugs that no hand-written directed test
// would have covered, because nobody thinks of every ordering.
//=====================================================================
module s24_tb_tier3;

    localparam time CLK_PERIOD = 10ns;
    localparam int  MAX_LATENCY = 80;

    logic clk = 0;
    always #(CLK_PERIOD/2) clk = ~clk;

    s24_if vif (.clk(clk));

    s24 uut (
        .clk   (clk),
        .reset (vif.reset),
        .start (vif.start),
        .y_out (vif.y_out)
    );

    //-----------------------------------------------------------------
    // Transaction: a description of one legal scenario.
    // VHDL has no direct equivalent - OSVVM supplies this as a library.
    //-----------------------------------------------------------------
    class s24_txn;
        rand int unsigned idle_before;   // cycles idle before start
        rand int unsigned start_width;   // how long start is held
        rand bit          inject_reset;  // abort mid-flight?
        rand int unsigned reset_at;      // when to abort

        constraint c_idle  { idle_before inside {[0:20]}; }
        constraint c_width { start_width inside {[1:3]};  }
        // dist weights the random choice: reset injection is the
        // interesting-but-rarer case.
        constraint c_rst   { inject_reset dist {0 := 8, 1 := 2};
                             reset_at inside {[1:MAX_LATENCY-1]}; }

        function string convert2string();
            return $sformatf("idle=%0d width=%0d rst=%0b @%0d",
                             idle_before, start_width, inject_reset, reset_at);
        endfunction
    endclass

    //-----------------------------------------------------------------
    // Functional coverage: the answer to "have I tested enough?"
    // This is the question directed testing cannot answer, and it is
    // the main reason industry moved to this methodology.
    //-----------------------------------------------------------------
    // Pass plain scalars, not a class handle. Dereferencing a class
    // member inside a coverpoint expression (coverpoint t.idle_before)
    // is badly supported across simulators and is the usual cause of a
    // confusing "syntax error" reported on one of the bins lines.
    // Scalar arguments are the portable idiom.
    covergroup cg_stimulus with function sample(int unsigned idle,
                                                int unsigned width,
                                                bit          rst);
        option.per_instance = 1;

        idle_cp : coverpoint idle {
            bins zero     = {0};
            bins b_short  = {[1:5]};
            bins b_medium = {[6:15]};
            bins b_long   = {[16:20]};
        }
        width_cp : coverpoint width {
            bins one  = {1};
            bins many = {[2:3]};
        }
        reset_cp : coverpoint rst {
            bins no_reset  = {0};
            bins has_reset = {1};
        }
        // Cross coverage: every combination, not just every value.
        // This is where the real gaps hide.
        cross idle_cp, width_cp, reset_cp;
    endgroup

    cg_stimulus cov = new();

    //-----------------------------------------------------------------
    // Driver
    //-----------------------------------------------------------------
    task automatic drive(s24_txn t);
        repeat (t.idle_before) @(vif.cb);

        vif.cb.start <= 1;
        repeat (t.start_width) @(vif.cb);
        vif.cb.start <= 0;

        if (t.inject_reset) begin
            repeat (t.reset_at) @(vif.cb);
            vif.cb.reset <= 1;
            repeat (2) @(vif.cb);
            vif.cb.reset <= 0;
        end else begin
            repeat (MAX_LATENCY + 2) @(vif.cb);
        end
    endtask

    //-----------------------------------------------------------------
    // Main loop. The SVA from Tier 2 still applies - reuse it here.
    // Randomisation supplies stimulus; assertions supply checking;
    // coverage tells you when to stop.
    //-----------------------------------------------------------------
    initial begin
        s24_txn t;

        vif.cb.reset <= 1;
        vif.cb.start <= 0;
        repeat (4) @(vif.cb);
        vif.cb.reset <= 0;

        t = new();
        repeat (200) begin
            assert (t.randomize())
                else $fatal(1, "randomization failed");
            cov.sample(t.idle_before, t.start_width, t.inject_reset);
            $display("[%0t] %s", $time, t.convert2string());
            drive(t);
        end

        $display("=== TIER3 done - coverage: %0.1f%% ===",
                 cov.get_inst_coverage());
        $finish;
    end

    initial begin
        #5ms;
        $fatal(1, "WATCHDOG timeout");
    end

endmodule


//=====================================================================
// WHAT THIS ACTUALLY BUYS YOU
//
// Feature                     VHDL-2008        SystemVerilog
// ------------------------------------------------------------------
// Self-checking               procedures       tasks           (equal)
// Bounded wait                wait..for        fork/join_any   (SV nicer)
// Race-free sampling          manual delay     clocking block  (SV wins)
// Temporal assertions         PSL              SVA             (SV wins)
// Constrained random          OSVVM library    built in        (SV wins)
// Functional coverage         OSVVM library    built in        (SV wins)
// OOP / layered testbench     none             classes         (SV only)
// Threading, mailboxes        limited          built in        (SV wins)
// Queues, assoc. arrays       clunky           built in        (SV wins)
//
//
// TWO THINGS WORTH KNOWING
//
// 1. You do not have to leave VHDL to get most of this.
//    PSL gives you temporal assertions in VHDL, and GHDL supports it
//    free (-fpsl, assertions written in -- psl comments). OSVVM gives
//    you constrained random, functional coverage and scoreboards as a
//    VHDL library. German FPGA shops in defence, rail and industrial
//    automation largely run exactly this stack - so for that market,
//    VHDL + OSVVM + PSL is not a compromise, it is the native idiom.
//
// 2. cocotb may be the better move for you right now.
//    Python already gives you classes, randomisation, queues and
//    coverage libraries; cocotb drives your existing VHDL through
//    GHDL; it is free; and it runs in GitHub Actions. You get most of
//    Tier 3's structure today, on your own machine, in a language you
//    already know - no commercial licence required.
//
//
// THE HONEST SUMMARY
//
// Tier 1 over good VHDL: not worth switching languages for.
// Tier 2 over Tier 1: a real step change, and the reason to learn SVA
//   regardless of which language you design in.
// Tier 3 over Tier 2: transformative on complex DUTs, pointless on
//   this one, and a months-long investment that is really a decision
//   to become a verification engineer.
//=====================================================================

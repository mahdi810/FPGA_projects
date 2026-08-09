----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 08/08/2026 11:26:08 AM
-- Design Name: 
-- Module Name: s24 - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
-- 
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
-- 
----------------------------------------------------------------------------------



LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
USE ieee.numeric_std.ALL;

ENTITY s24 IS
    PORT (
        clk : IN STD_LOGIC;
        reset : IN STD_LOGIC;
        start : IN STD_LOGIC;
        y_out : OUT STD_LOGIC
    );
END ENTITY;

ARCHITECTURE behavioral OF s24 IS
    TYPE state_type IS (idle, state_l, state_h);
    SIGNAL state, next_state : state_type;

    SUBTYPE clk_cycle_type IS INTEGER RANGE 0 TO 16;
    TYPE no_of_clk_array IS ARRAY(0 TO 6) OF clk_cycle_type;
    SIGNAL no_clk_cycles : no_of_clk_array := (7, 6, 5, 4, 3, 1, 0);

    SUBTYPE int16_t IS INTEGER RANGE -32768 TO 32767;
    SUBTYPE counter_type IS INTEGER RANGE 0 TO 15;
    SIGNAL timer : counter_type := 0;
    SIGNAL cnt, cnt_i : int16_t := 0;
BEGIN

    -- sequential process 
    -- timed state machine 
    seq_p : PROCESS (clk, reset)
        VARIABLE count : counter_type := 0;
    BEGIN
        IF rising_edge(clk) THEN
            IF reset = '1' THEN
                state <= idle;
                count := 0;
                cnt <= 0;
            ELSE
                IF count = timer THEN
                    state <= next_state;
                    cnt <= cnt_i;
                    count := 0;
                ELSE
                    count := count + 1;
                END IF;
            END IF;
        END IF;
    END PROCESS;

    -- combinational process 
    comb_p : PROCESS (state, cnt_i, start, cnt)
    BEGIN
        -- default cases 
        next_state <= state;
        cnt_i <= cnt;
        timer <= 0;

        CASE state IS
            WHEN idle =>
                y_out <= '0';
                cnt_i <= 0;
                IF start = '1' THEN
                    next_state <= state_l;
                END IF;
            WHEN state_l =>
                y_out <= '0';
                timer <= no_clk_cycles(cnt);
                next_state <= state_h;
            WHEN state_h =>
                y_out <= '1';
                timer <= no_clk_cycles(cnt_i);
                cnt_i <= cnt + 1;

                IF start = '0' THEN
                    next_state <= idle;
                    cnt_i <= 0;
                ELSE
                    IF cnt = 6 THEN
                        cnt_i <= 0;
                    END IF;
                    next_state <= state_l;
                END IF;
        END CASE;
    END PROCESS;

END ARCHITECTURE behavioral;
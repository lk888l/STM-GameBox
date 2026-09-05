proc word {address} { return [lindex [read_memory $address 32 1] 0] }
proc byte {address} { return [lindex [read_memory $address 8 1] 0] }
init
set failure [catch {
    halt
    verify_image {@ELF@}
    foreach {key pin bit} {UP 7 0 DOWN 5 1 LEFT 6 2 RIGHT 4 3 JUMP 12 4 FUNC 13 5 ENTER 14 6 BACK 15 7} {
        reset run
        sleep 1500
        set cr [word [expr {0x40010c00 + ($pin / 8) * 4}]]
        if {(($cr >> (($pin % 8) * 4)) & 15) != 8} { error "$key is not a pull-up/down input" }
        if {([word 0x40010c0c] & (1 << $pin)) == 0} { error "$key is not pulled up" }
        if {([word 0x40010c08] & (1 << $pin)) == 0} { error "$key is physically held; release controls" }
        set presses 0
        set releases 0
        set events_before [word $KEY_EVENTS_PROCESSED]
        for {set i 0} {$i < @COUNT@} {incr i} {
            set held_before [byte $HELD_KEYS]
            # BSRR changes only ODR: in input mode this selects the weak pull resistor.
            mww 0x40010c10 [expr {1 << ($pin + 16)}]
            sleep @HOLD@
            if {([word 0x40010c08] & (1 << $pin)) != 0} { error "$key external pull-up prevents injection" }
            set held_pressed [byte $HELD_KEYS]
            if {($held_before & (1 << $bit)) == 0 && ($held_pressed & (1 << $bit)) != 0} { incr presses }
            mww 0x40010c10 [expr {1 << $pin}]
            sleep @HOLD@
            if {($held_pressed & (1 << $bit)) != 0 && ([byte $HELD_KEYS] & (1 << $bit)) == 0} { incr releases }
        }
        sleep 350
        set events [expr {[word $KEY_EVENTS_PROCESSED] - $events_before}]
        echo "KEY $key presses=$presses releases=$releases events=$events scan_gap_us=[word $MAX_BUTTON_SCAN_GAP_US] press_age_ms=[word $MAX_KEY_PRESS_AGE_MS] render_us=[word $MAX_RENDER_TIME_US] samples=[word $BUTTON_SAMPLES] frames=[word $RENDERED_FRAMES] input_drops=[word $DROPPED_KEY_EVENTS] uart_drops=[word $DROPPED_UART_EVENTS] oled_errors=[word $OLED_ERRORS] transfers=[word $OLED_TRANSFERS]"
    }
} reason]
# A reset restores every input pull even if an assertion or SWD access failed.
reset run
if {$failure} { echo "REGRESSION_ERROR $reason" } else { echo "REGRESSION_COMPLETE" }
shutdown

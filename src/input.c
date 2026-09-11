#include "beelz.h"

void input_init(void) {
    /* Maple bus is brought up by KOS core init; nothing extra needed for
     * a single standard controller in port A. */
}

void input_update(bz_input_t *in) {
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    in->buttons_prev = in->buttons;

    if (dev) {
        cont_state_t *st = (cont_state_t *)maple_dev_status(dev);
        if (st) {
            in->buttons = st->buttons;
            in->ltrig = st->ltrig;
            in->rtrig = st->rtrig;
            in->joyx = st->joyx;
            in->joyy = st->joyy;
        }
    } else {
        in->buttons = 0;
        in->ltrig = in->rtrig = 0;
        in->joyx = in->joyy = 0;
    }

    in->pressed = in->buttons & ~in->buttons_prev;
    in->released = ~in->buttons & in->buttons_prev;
}

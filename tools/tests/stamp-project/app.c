/* Minimal application for the stamp tool tests. The reserved vector table
 * words (0x1C-0x2B) are zero. .data makes the image include a load region
 * after the code. */
void reset_handler(void);

volatile unsigned int counter = 5;

__attribute__((section(".isr_vector"), used))
void (*const vectors[16])(void) = {
    (void (*)(void))0x20005000, reset_handler, reset_handler, reset_handler,
};

void reset_handler(void)
{
    for (;;)
        counter++;
}

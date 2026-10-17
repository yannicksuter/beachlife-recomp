// GLOBAL: FIXTURE 0x00403000
volatile unsigned int fixture_result;
// FUNCTION: FIXTURE 0x00401000
extern "C" unsigned int fixture_mix(unsigned int value) {
    return (value * 33u) ^ 0x51a9u;
}
extern "C" void fixture_entry() {
    fixture_result = fixture_mix(7u);
}

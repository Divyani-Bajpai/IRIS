#include <windows.h>

int main() {
    // A quiet child process gives the collector a second CPU-time sample
    // without generating background load on the user's machine.
    Sleep(10000);
    return 0;
}

# experimentoc
Small programs to make life easier for the user.

# Monitor de Memoria para macOS (memoriuses)

Este es un sencillo monitor de memoria para macOS que se ejecuta en la terminal utilizando la librería ncurses. Muestra información en tiempo real sobre el uso de RAM y SWAP, incluyendo un historial gráfico del uso de RAM.

## Características

*   Muestra la memoria RAM total, usada, libre, inactiva, wired y comprimida.
*   Muestra el uso de memoria SWAP total y usada.
*   Barras de progreso visuales para el uso de RAM y SWAP.
*   Gráfico histórico del uso de RAM (últimos 60 segundos).
*   Información del sistema como número de CPUs y uptime.
*   Colores para indicar niveles de uso de memoria (bajo, medio, alto).
*   Interfaz de usuario en ncurses.

## Requisitos

*   macOS
*   Compilador GCC (o Clang compatible con GCC)
*   Librería ncurses (generalmente preinstalada en macOS)

## Compilación

Para compilar `memoriuses.c`, ejecuta el siguiente comando en tu consola:

```bash
gcc -Wall -Wextra -g3 memoriuses.c -o memoria -lncurses

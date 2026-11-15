#include <ncurses.h>

int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int y = 0, x = 0;
    getmaxyx(stdscr, y, x);

    mvprintw(y / 2, (x - 22) / 2, "Press 'q' to quit.");
    refresh();

    while (getch() != 'q') {
    }

    endwin();
    return 0;
}

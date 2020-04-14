#include <stdio.h>
#include <time.h>
#include <unistd.h> // pid, usleep
#include <stdlib.h> // srand
#include <sys/mman.h> // shm 
#include <fcntl.h> // O_ constants
#include <ncurses.h>
#include <pthread.h>
#include <semaphore.h>

#include "bot.h"

int main(void)
{
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, max_y, max_x);

    // trying to join the lobby, if the server is up and running
    int waiting = 0, fd = 0;
    while(1)
    {
        clear();
        mvprintw(max_y/2-1, max_x/2-5, "Joining the lobby");
        mvprintw(max_y/2, max_x/2-2, ".");
        refresh();
        usleep(250*MS);
        mvprintw(max_y/2, max_x/2-2, "..");
        refresh();
        usleep(250*MS);
        mvprintw(max_y/2, max_x/2-2, "...");
        refresh();
        usleep(250*MS);
        fd = shm_open("lobby", O_RDWR, 0600);
        if (fd > 0) 
            break;
        if (waiting++ > 50)
        {
            clear();
            mvprintw(max_y/2-1, max_x/2-5, "Couldn't join the lobby");
            refresh();
            usleep(3000*MS);
            return 1;
        }
    }

    if (ftruncate(fd, sizeof(struct lobby_t)) < 0)
    {
        perror("ftruncate(lobby)");
        return 1;
    }
    game_lobby = mmap(NULL, sizeof(struct lobby_t), PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (*(int *)game_lobby == -1)
    {
        perror("mmap(lobby)");
        return 1;
    }

    if (join_the_game((int)getpid()) == 0)
    {
        munmap(game_lobby, sizeof(struct lobby_t));
        return 1;
    }

    pthread_create(&print_board_thread, NULL, print_board, NULL);
    pthread_create(&key_events_thread, NULL, key_events, NULL);
    pthread_create(&move_bot_thread, NULL, move_bot, NULL);

    pthread_join(key_events_thread, NULL);

    return 0;
}

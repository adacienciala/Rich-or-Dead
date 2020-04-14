#include <stdio.h>
#include <time.h>
#include <unistd.h> // pid, usleep
#include <stdlib.h> // srand
#include <signal.h> // kill(2)
#include <sys/mman.h> // shm 
#include <ncurses.h>
#include <pthread.h>
#include <semaphore.h>

#include "server.h"

int main(void)
{
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, max_y, max_x);

    if (load_board("board.txt"))
    {
        perror("load_board");
        return 1;
    }
    if (set_up_game((int)getpid())) return 1;

    pthread_t print_board_thread, player_in_thread, player_out_thread, key_events_thread;

    pthread_create(&print_board_thread, NULL, print_board, NULL);
    pthread_create(&player_in_thread, NULL, player_in, NULL);
    pthread_create(&player_out_thread, NULL, player_out, NULL);
    pthread_create(&key_events_thread, NULL, key_events, NULL);

    sem_init(&game_end, 0, 0);
    sem_wait(&game_end);

    // kick all the players and clean up their stuff
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (game_data.players_shared[id] != NULL)
        {
            kill(game_data.players[id].PID, 1);
            sem_destroy(&game_data.players_shared[id]->player_moved);
            sem_destroy(&game_data.players_shared[id]->player_continue);
            int pid = game_data.players_shared[id]->PID;
            munmap(game_data.players_shared[id], sizeof(struct player_data_t));
            game_data.players_shared[id] = NULL;
            player_pid_shm("unlink", pid);
        }
    }

    // clean up beasts' stuff
    for (int id = 0; id < MAX_BEASTS; ++id)
    {
        if (game_data.beasts[id].coords.x >= 0)
        {
            sem_destroy(&game_data.beasts[id].go);
            sem_destroy(&game_data.beasts[id].done);
            pthread_cancel(game_data.beasts[id].beast_thread);
        }
    }

    // clean up lobby's stuff
    sem_destroy(&game_data.lobby->ask);
    sem_destroy(&game_data.lobby->leave);
    sem_destroy(&game_data.lobby->joined);
    sem_destroy(&game_data.lobby->exited);

    // clean up game's stuff
    munmap(game_data.lobby, sizeof(struct lobby_t));
    shm_unlink("lobby");

    pthread_mutex_destroy(&game_data.game_mutex);
    sem_destroy(&game_end);

    endwin();
    return 0;
}

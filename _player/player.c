#define _GNU_SOURCE // pthread_tryjoin_np
#include <stdio.h>
#include <unistd.h> // pid, usleep
#include <string.h>
#include <sys/mman.h> // shm
#include <fcntl.h> // O_ constants
#include <ncurses.h>
#include <pthread.h>
#include <semaphore.h>

#include "player.h"

// board stuff
char board[ROWS][COLUMNS];
int free_spots;
int max_x, max_y;

// player stuff
pthread_t print_board_thread, key_events_thread;

void* key_events(void *none)
{
    // constantly listening to the keyboard
    int a;
    while(1)
    {
        a = getch();
        switch(a)
        {
            // quit the game
            case 'q':
            case 'Q':
                exit_the_game(player_data->PID);
                return NULL;
            // set your desired action
            case KEY_UP:
            case 'w':
            case 'W':
                player_data->direction = NORTH;
                break;
            case KEY_RIGHT:
            case 'd':
            case 'D':
                player_data->direction = EAST;
                break;
            case KEY_DOWN:
            case 's':
            case 'S':
                player_data->direction = SOUTH;
                break;
            case KEY_LEFT:
            case 'a':
            case 'A':
                player_data->direction = WEST;
        }
        sem_post(&player_data->player_moved);
        sem_wait(&player_data->player_continue);
    }
}

void* print_board(void* none)
{
    start_color();

    init_color(COLOR_BLUE, 100, 400, 700);
    init_color(COLOR_YELLOW, 1000, 1000, 0);
    init_color(COLOR_WHITE, 1000, 1000, 1000);

    init_pair(C_PLAYER, COLOR_WHITE, COLOR_BLUE);
    init_pair(C_WALL, COLOR_BLACK, COLOR_BLACK);
    init_pair(C_BEAST, COLOR_RED, COLOR_WHITE);
    init_pair(C_MONEY, COLOR_BLACK, COLOR_YELLOW);
    init_pair(C_CAMPSITE, COLOR_YELLOW, COLOR_GREEN);
    init_pair(C_DROPPED, COLOR_GREEN, COLOR_YELLOW);
    init_pair(C_DEFAULT, COLOR_BLACK, COLOR_WHITE);
    init_pair(C_TERMINAL, COLOR_WHITE, COLOR_BLACK);

    // terminal background
    attron(COLOR_PAIR(C_DEFAULT));
    for (int i = 0; i < max_y; ++i)
    {
        for (int j = 0; j < max_x; ++j)
        {
            mvprintw(i, j, " ");
        }
    }

    while(1)
    {
        // discovered board
        for (int i = 0; i < ROWS; ++i)
        {
            for (int j = 0; j < COLUMNS; ++j)
            {
                // the board and once seen artefacts
                switch (player_data->player_board[i][j])
                {
                    case '|':
                        attron(COLOR_PAIR(C_WALL));
                        break;
                    case 'c':
                    case 't':
                    case 'T':
                        attron(COLOR_PAIR(C_MONEY));
                        break;
                    case 'A':
                        attron(COLOR_PAIR(C_CAMPSITE));
                        break;
                    case 'D':
                        attron(COLOR_PAIR(C_DROPPED));
                        break;
                    case '.':
                        player_data->player_board[i][j] = ' ';
                    default:
                        attron(COLOR_PAIR(C_DEFAULT));
                }
                mvprintw(i, j, "%c", player_data->player_board[i][j]);
            }
        }

        // current minimap
        int x = player_data->coords.x;
        int y = player_data->coords.y;
        for (int i = 0; i < SIGHT; ++i)
        {
            for (int j = 0; j < SIGHT; ++j)
            {
                switch (player_data->player_minimap[i][j])
                {
                    case '|':
                        attron(COLOR_PAIR(C_WALL));
                        break;
                    case 'c':
                    case 't':
                    case 'T':
                        attron(COLOR_PAIR(C_MONEY));
                        break;
                    case 'A':
                        attron(COLOR_PAIR(C_CAMPSITE));
                        break;
                    case 'D':
                        attron(COLOR_PAIR(C_DROPPED));
                        break;
                    case '.':
                        player_data->player_minimap[i][j] = ' ';
                    case ' ':
                    case '#':
                        attron(COLOR_PAIR(C_DEFAULT));
                        break;
                    case '*':
                        attron(COLOR_PAIR(C_BEAST));
                        break;
                    default:
                        attron(COLOR_PAIR(C_PLAYER));
                }
                if ((y - 2 + i) < 0 || (x - 2 + j) < 0 || (y - 2 + i) >= ROWS || (x - 2 + j) >= COLUMNS)
                    continue;
                else 
                    mvprintw(y-2+i, x-2+j, "%c", player_data->player_minimap[i][j]);
                if (i == 2 && j == 2)
                {
                    attron(COLOR_PAIR(C_PLAYER));
                    mvprintw(y-2+i, x-2+j, "%d", player_data->ID);
                }
            }
        }

        // server info
        int cur_row = 0, cur_col = COLUMNS + 4;
        attron(COLOR_PAIR(C_DEFAULT));
        mvprintw(++cur_row, cur_col, "Server's PID: %d", player_data->server_PID);
        if (player_data->campsite.x >= 0) 
            mvprintw(++cur_row, cur_col + 1, "Campsite X/Y: %02d/%02d  ", player_data->campsite.x, player_data->campsite.y);
        else 
            mvprintw(++cur_row, cur_col + 1, "Campsite X/Y: unknown");
        mvprintw(++cur_row, cur_col + 1, "Round number: %d", player_data->round_counter);

        // players info
        ++cur_row;
        mvprintw(++cur_row, cur_col, "Player:");
        mvprintw(++cur_row, cur_col + 1, "Number");
        mvprintw(cur_row, cur_col + 13, "%d", player_data->ID);
        mvprintw(++cur_row, cur_col + 1, "Type");
        mvprintw(cur_row, cur_col + 13, "%s", "HUMAN");
        mvprintw(++cur_row, cur_col + 1, "Curr X/Y");
        mvprintw(cur_row, cur_col + 13, "%02d/%02d", player_data->coords.x, player_data->coords.y);
        mvprintw(++cur_row, cur_col + 1, "Deaths");
        mvprintw(cur_row, cur_col + 13, "%d    ", player_data->deaths);
        mvprintw(++cur_row, cur_col + 1, "Coins");
        mvprintw(++cur_row, cur_col + 2, "carried");
        mvprintw(cur_row, cur_col + 13, "%d    ", player_data->coins_carried);
        mvprintw(++cur_row, cur_col + 2, "brought");
        mvprintw(cur_row, cur_col + 13, "%d    ", player_data->coins_brought);

        // legend
        cur_row += 2;
        mvprintw(++cur_row, cur_col, "Legend:");
        attron(COLOR_PAIR(C_PLAYER));
        mvprintw(++cur_row, cur_col + 1, "1234");
        attron(COLOR_PAIR(C_WALL));
        mvprintw(++cur_row, cur_col + 1, " ");
        attron(COLOR_PAIR(C_DEFAULT));
        mvprintw(++cur_row, cur_col + 1, "#");
        attron(COLOR_PAIR(C_BEAST));
        mvprintw(++cur_row, cur_col + 1, "*");
        attron(COLOR_PAIR(C_MONEY));
        mvprintw(++cur_row, cur_col + 1, "c");
        mvprintw(++cur_row, cur_col + 1, "t");
        mvprintw(++cur_row, cur_col + 1, "T");
        attron(COLOR_PAIR(C_CAMPSITE));
        mvprintw(++cur_row, cur_col + 1, "A");
        attron(COLOR_PAIR(C_DROPPED));
        mvprintw(++cur_row, cur_col + 1, "D");
        cur_row -= 9;
        attron(COLOR_PAIR(C_DEFAULT));
        mvprintw(++cur_row, cur_col + 7, "- players");
        mvprintw(++cur_row, cur_col + 7, "- wall");
        mvprintw(++cur_row, cur_col + 7, "- bush (slow down)");
        mvprintw(++cur_row, cur_col + 7, "- wild beast");
        mvprintw(++cur_row, cur_col + 7, "- one coin");
        mvprintw(++cur_row, cur_col + 7, "- treasure (10 coins)");
        mvprintw(++cur_row, cur_col + 7, "- large treasure (50 coins)");
        mvprintw(++cur_row, cur_col + 7, "- campsite");
        mvprintw(++cur_row, cur_col + 7, "- dropped treasure");

        refresh();
        usleep(125 * MS);
    }
}

int join_the_game(int pid)
{
    // wait for you turn to ask the server to join
    sem_wait(&game_lobby->ask);

    // try to take a sit in the lobby
    int free_spot = -1;
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (game_lobby->queue[id].want_to == BE_EMPTY)
        {
            free_spot = id;
            break;
        }
    }

    if (free_spot == -1)
    {
        // let someone else ask
        sem_post(&game_lobby->ask);

        //get out
        clear();
        mvprintw(max_y/2-1, max_x/2-5, "No free spots!");
        refresh();
        usleep(3000*MS);

        return 0;
    }

    // make yourself comfortable
    game_lobby->queue[free_spot].want_to = JOIN;
    game_lobby->queue[free_spot].type = HUMAN;
    game_lobby->queue[free_spot].PID = pid;

    // let them know you're waiting
    sem_post(&game_lobby->joined);

    // politely wait to leave the lobby and start playing
    int waiting = 0;
    while(1)
    {
        clear();
        mvprintw(max_y/2-1, max_x/2-8, "Joining the game");
        mvprintw(max_y/2, max_x/2-2, ".");
        refresh();
        usleep(250*MS);
        mvprintw(max_y/2, max_x/2-2, "..");
        refresh();
        usleep(250*MS);
        mvprintw(max_y/2, max_x/2-2, "...");
        refresh();
        usleep(250*MS);
        if (sem_trywait(&game_lobby->leave) == 0) 
            break;
        if (waiting++ > 20)
        {
            clear();
            mvprintw(max_y/2-1, max_x/2-11, "Couldn't join the game");
            refresh();
            usleep(3000*MS);

            return 0;
        }
    }

    // green light, let's get the ball rolling!
    int fd = player_pid_shm("open", pid);
    if (fd < 0)
    {
        perror("shm_open(player)");
        return 0;
    }
    if (ftruncate(fd, sizeof(struct player_data_t)) < 0)
    {
        perror("ftruncate(player)");
        return 0;
    }
    player_data = mmap(NULL, sizeof(struct player_data_t), PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (*(int *)player_data == -1)
    {
        perror("mmap(player)");
        return 0;
    }
    return 1;
}

int player_pid_shm(char* action, int pid)
{
    char name[20];
    sprintf(name, "player_%d", pid);
    if (strcmp(action, "open") == 0)
    {
        return shm_open(name, O_RDWR, 0600);
    }
    else return -1;
}

int exit_the_game(int pid)
{
    // wait for you turn to ask the server to exit
    sem_wait(&game_lobby->ask);
    pthread_cancel(print_board_thread);
    clear();

    // find your spot in the lobby
    int spot = -1;
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (game_lobby->queue[id].PID == pid)
        {
            spot = id;
            break;
        }
    }

    attron(COLOR_PAIR(C_TERMINAL));
    if (spot == -1)
    {
        clear();
        mvprintw(max_y/2-1, max_x/2-7, "Are you even real?");
        refresh();
        usleep(3000*MS);

        munmap(game_lobby, sizeof(struct lobby_t));

        return 1;
    }

    // tell the server you're not having fun
    game_lobby->queue[spot].want_to = EXIT;

    // let them know you're lame
    sem_post(&game_lobby->exited);

    // politely wait to leave the lobby and have a life or sth
    int waiting = 0;
    while(1)
    {
        clear();
        mvprintw(max_y/2-1, max_x/2-5, "Exiting the game");
        mvprintw(max_y/2, max_x/2-2, ".");
        refresh();
        usleep(250*MS);
        mvprintw(max_y/2, max_x/2-2, "..");
        refresh();
        usleep(250*MS);
        mvprintw(max_y/2, max_x/2-2, "...");
        refresh();
        usleep(250*MS);
        if (sem_trywait(&game_lobby->leave) == 0) 
            break;
        if (waiting++ > 20)
        {
            clear();
            mvprintw(max_y/2-1, max_x/2-15, "Couldn't exit the game, so we rage quit!");
            refresh();
            usleep(3000*MS);

            munmap(game_lobby, sizeof(struct lobby_t));
            return 1;
        }
    }

    // pack your things, you're not welcomed here anymore
    munmap(game_lobby, sizeof(struct lobby_t));

    return 1;
}
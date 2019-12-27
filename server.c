#define _GNU_SOURCE // pthread_tryjoin_np
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ncurses.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h> // kill(2)
#include <sys/mman.h>
#include <sys/types.h>

#define MS 1000

// colors stuff
#define C_PLAYER 1
#define C_WALL 2
#define C_BEAST 3
#define C_MONEY 4
#define C_CAMPSITE 5
#define C_DROPPED 6
#define C_DEFAULT 7


// board stuff
#define ROWS 25
#define COLUMNS 51
#define FREE_SPACE(a) ( a == ' ' || a == '.' )

char board[ROWS][COLUMNS];
int max_x, max_y;


// game stuff
#define MAX_PLAYERS 4
#define MAX_BEASTS 25
#define SIGHT 5

enum type_t { HUMAN, CPU };
enum directions_t { STAY, NORTH, EAST, SOUTH, WEST };
enum state_t { BE_EMPTY, CONTINUE, JOIN, EXIT };

struct coords_t
{
    int x;
    int y;
};

struct beast_t
{
    pthread_t beast_thread;
    sem_t go;
    sem_t done;
    struct coords_t coords;
    enum directions_t direction[2];
};

struct player_data_t
{
    int ID;
    int PID;
    int server_PID;
    enum type_t type;
    struct coords_t coords;
    struct coords_t campsite;
    int round_counter;
    enum directions_t direction;
    int slowed_down;
    int deaths;
    int coins_carried;
    int coins_brought;

    // used only in players' shms
    char player_minimap[SIGHT][SIGHT];
    char player_board[ROWS][COLUMNS];
    sem_t player_moved;
    sem_t player_continue;
};

struct queue_t
{
    enum state_t want_to;
    enum type_t type;
    int PID;
};

struct lobby_t
{
    struct queue_t queue[MAX_PLAYERS];

    sem_t ask;
    sem_t leave;

    sem_t joined;
    sem_t exited;
};

struct game_data_t
{
    struct lobby_t* lobby;

    int PID;
    int round_counter;
    struct coords_t campsite;

    int players_counter;
    struct player_data_t* players_shared[MAX_PLAYERS];
    struct player_data_t players[MAX_PLAYERS];

    int beasts_counter;
    struct beast_t beasts[MAX_BEASTS];

    int chests[ROWS][COLUMNS];
} game_data;

int load_board(char *filename);
void set_up_game(int PID);
void* print_board(void* none);
void* key_events(void* none);
void* player_in(void* none);
void* player_out(void* none);
void* rounds_up(void* none);
int player_pid_shm(char* action, int pid);

//int draw_line_straight(int x0, int y0, int x1, int y1);
int draw_line_low(int x0, int y0, int x1, int y1);
int draw_line_high(int x0, int y0, int x1, int y1);
int draw_line(int x0, int y0, int x1, int y1, int id);
void* beast_action(void* id);


// server's stuff
sem_t game_end;

// DEVELOPMENT QUESTIONS
// chyba niepotrzebne mi players_counter tak po prawdzie
// - boty + algorytm do nich
// - pieprzone mutexy bo dyskoteka od refreshow ;-;
// - ograniczenie spawnowania -> free_space w strukturze?
// - pousuwac z playera niepotrzebne includy

int main(void)
{
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, max_y, max_x);

    load_board("board.txt");
    set_up_game((int)getpid());

    pthread_t print_board_thread, player_in_thread, player_out_thread, key_events_thread;

    pthread_create(&print_board_thread, NULL, print_board, NULL);
    pthread_create(&player_in_thread, NULL, player_in, NULL);
    pthread_create(&player_out_thread, NULL, player_out, NULL);
    pthread_create(&key_events_thread, NULL, key_events, NULL);

    sem_init(&game_end, 0, 0);
    sem_wait(&game_end);

    // kick all the players and clean their stuff up
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

    // clean up lobby's semaphores, lobby's shm and game's semaphore
    sem_destroy(&game_data.lobby->ask);
    sem_destroy(&game_data.lobby->leave);
    sem_destroy(&game_data.lobby->joined);
    sem_destroy(&game_data.lobby->exited);

    munmap(game_data.lobby, sizeof(struct lobby_t));
    shm_unlink("lobby");

    sem_destroy(&game_end);

    endwin();
    return 0;
}

int player_pid_shm(char* action, int pid)
{
    char name[20];
    sprintf(name, "player_%d", pid);
    if (strcmp(action, "open") == 0)
    {
        return shm_open(name, O_CREAT | O_RDWR, 0600);
    }
    else if (strcmp(action, "unlink") == 0)
    {
        return shm_unlink(name);
    }
    else return -1;
}

void set_up_game(int PID)
{
    // zero every counter
    game_data.round_counter = 0;
    game_data.players_counter = 0;
    game_data.beasts_counter = 0;

    // set the lobby, PID, coords and stuff
    int fd = shm_open("lobby", O_CREAT | O_RDWR, 0600);
    ftruncate(fd, sizeof(struct lobby_t));
    game_data.lobby = mmap(NULL, sizeof(struct queue_t), PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        game_data.players_shared[i] = NULL;

        game_data.lobby->queue[i].want_to = BE_EMPTY;
        game_data.lobby->queue[i].type = CPU;
        game_data.lobby->queue[i].PID = -1;
    }

    sem_init(&game_data.lobby->ask, 1, 1);
    sem_init(&game_data.lobby->leave, 1, 0);
    sem_init(&game_data.lobby->joined, 1, 0);
    sem_init(&game_data.lobby->exited, 1, 0);

    game_data.PID = PID;
    while(1)
    {
        game_data.campsite.x = rand() % COLUMNS;
        game_data.campsite.y = rand() % ROWS;
        if (FREE_SPACE(board[game_data.campsite.y][game_data.campsite.x]))
        {
            board[game_data.campsite.y][game_data.campsite.x] = 'A';
            break;
        }
    }
    for (int i = 0; i < MAX_BEASTS; ++i)
    {
        game_data.beasts[i].coords.x = -2;
        game_data.beasts[i].coords.y = -2;
    }
    for (int i = 0; i < ROWS; ++i)
    {
        for (int j = 0; j < COLUMNS; ++j)
        {
            game_data.chests[i][j] = 0;
        }
    }
}

int load_board(char *filename)
{
    if (!filename) return 1;

    FILE *f;
    if ((f=fopen(filename, "r")) == NULL ) return 2;

    int row = 0;

    while(!feof(f) && row < ROWS)
    {
        fscanf(f, "%s", board[row++]);
    }

    fclose(f);
    return 0;
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

    pthread_t rounds_up_thread;

    while(1)
    {
        clear();
        // terminal background
        attron(COLOR_PAIR(C_DEFAULT));
        for (int i = 0; i < max_y; ++i)
        {
            for (int j = 0; j < max_x; ++j)
            {
                mvprintw(i, j, " ");
            }
        }

        // board
        for (int i = 0; i < ROWS; ++i)
        {
            for (int j = 0; j < COLUMNS; ++j)
            {
                // the board and artefacts
                switch (board[i][j])
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
                        board[i][j] = ' ';
                    default:
                        attron(COLOR_PAIR(C_DEFAULT));
                }
                mvprintw(i, j, "%c", board[i][j]);
            }
        }

        // players
        attron(COLOR_PAIR(C_PLAYER));
        for (int id = 0; id < MAX_PLAYERS; ++id)
        {
            if (game_data.players_shared[id] != NULL)
            {
                mvprintw(game_data.players[id].coords.y, game_data.players[id].coords.x, "%d", id+1);
            }
        }

        // beasts
        attron(COLOR_PAIR(C_BEAST));
        for (int id_beast = 0; id_beast < MAX_BEASTS; ++id_beast)
        {
            if (game_data.beasts[id_beast].coords.x >= 0)
            {
                mvprintw(game_data.beasts[id_beast].coords.y, game_data.beasts[id_beast].coords.x, "*");
            }
        }

        // server info
        int cur_row = 0, cur_col = COLUMNS + 4;
        attron(COLOR_PAIR(C_DEFAULT));
        mvprintw(++cur_row, cur_col, "Server's PID: %d", game_data.PID);
        mvprintw(++cur_row, cur_col + 1, "Campsite X/Y: %02d/%02d", game_data.campsite.x, game_data.campsite.y);
        mvprintw(++cur_row, cur_col + 1, "Round number: %d", game_data.round_counter);

        // players info
        ++cur_row;
        for (int id = 0; id < MAX_PLAYERS; ++id)
        {
            int player_connected = 0;
            if (game_data.players_shared[id] != NULL) player_connected = 1;

            mvprintw(++cur_row, cur_col, "Parameter:");
            mvprintw(cur_row, cur_col + 13 + (id * 10), "Player%d", id + 1);
            mvprintw(++cur_row, cur_col + 1, "PID");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d    ", game_data.players_shared[id]->PID);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c    ", '-');
            mvprintw(++cur_row, cur_col + 1, "Type");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%s    ", (game_data.players_shared[id]->type == CPU) ? "CPU": "HUMAN");
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c    ", '-');
            mvprintw(++cur_row, cur_col + 1, "Curr X/Y");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%02d/%02d", game_data.players_shared[id]->coords.x, game_data.players_shared[id]->coords.y);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%s", "--/--");
            mvprintw(++cur_row, cur_col + 1, "Deaths");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d    ", game_data.players_shared[id]->deaths);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c    ", '-');
            mvprintw(++cur_row, cur_col + 1, "Coins");
            mvprintw(++cur_row, cur_col + 2, "carried");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d    ", game_data.players_shared[id]->coins_carried);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c    ", '-');
            mvprintw(++cur_row, cur_col + 2, "brought");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d    ", game_data.players_shared[id]->coins_brought);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c    ", '-');
            cur_row -= 8;
        }

        // legend
        cur_row += 9;
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
        mvprintw(++cur_row, cur_col + 7, "%d - beasts", game_data.beasts_counter);

        refresh();
        game_data.round_counter++;
        pthread_create(&rounds_up_thread, NULL, rounds_up, NULL);
        usleep(125 * MS);
        pthread_join(rounds_up_thread, NULL);
    }
}

void* key_events(void *none)
{
    // constantly listening to the keyboard
    char a;
    while(1)
    {
        a = getchar();
        switch(a)
        {
            // quit the game
            case 'q':
            case 'Q':
                sem_post(&game_end);
                return NULL;
            // add a new beast
            case 'b':
            case 'B':
                for (int id_beast = 0; id_beast < MAX_BEASTS; ++id_beast)
                {
                    if (game_data.beasts[id_beast].coords.x < 0)
                    {
                        sem_init(&game_data.beasts[id_beast].go, 0, 0);
                        sem_init(&game_data.beasts[id_beast].done, 0, 0);
                        while(1)
                        {
                            game_data.beasts[id_beast].coords.x = rand() % COLUMNS;
                            game_data.beasts[id_beast].coords.y = rand() % ROWS;
                            if (FREE_SPACE(board[game_data.beasts[id_beast].coords.y][game_data.beasts[id_beast].coords.x])) break;
                        }
                        game_data.beasts_counter++;
                        pthread_create(&game_data.beasts[id_beast].beast_thread, NULL, beast_action, &id_beast);
                        break;
                    }
                }
                break;
            // add a coin/treasure/large treasure
            case 'c':
            case 't':
            case 'T':
                while (1)
                {
                    int x = rand() % COLUMNS;
                    int y = rand() % ROWS;
                    if (FREE_SPACE(board[y][x]))
                    {
                        board[y][x] = a;
                        break;
                    }
                }
                break;
        }
    }
}

void* player_in(void* none)
{
    while (1)
    {
        // waiting for someone to ask to join the game
        sem_wait(&game_data.lobby->joined);

        if (game_data.players_counter < MAX_PLAYERS)
        {
            for (int id = 0; id < MAX_PLAYERS; ++id)
            {
                // looking for the joined player
                if (game_data.lobby->queue[id].want_to == JOIN)
                {
                    // making a personalized shared memory with their PID
                    int fd = player_pid_shm("open", game_data.lobby->queue[id].PID);
                    ftruncate(fd, sizeof(struct player_data_t));
                    game_data.players_shared[id] = mmap(NULL, sizeof(struct player_data_t), PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);

                    // setting up server's respecting struct
                    game_data.players[id].ID = id+1;
                    game_data.players[id].PID = game_data.lobby->queue[id].PID;
                    game_data.players[id].server_PID = game_data.PID;
                    game_data.players[id].type = game_data.lobby->queue[id].type;

                    int x = -1, y = -1;
                    while (1)
                    {
                        x = rand() % COLUMNS;
                        y = rand() % ROWS;
                        if (FREE_SPACE(board[y][x])) break;
                    }
                    game_data.players[id].coords.x = x;
                    game_data.players[id].coords.y = y;

                    game_data.players[id].campsite.x = -4;
                    game_data.players[id].campsite.y = -4;

                    game_data.players[id].round_counter = game_data.round_counter;
                    game_data.players[id].direction = STAY;
                    game_data.players[id].slowed_down = 0;
                    game_data.players[id].deaths = 0;
                    game_data.players[id].coins_carried = 0;
                    game_data.players[id].coins_brought = 0;

                    // setting up player's struct and (mini)maps
                    game_data.players_shared[id]->ID = game_data.players[id].ID;
                    game_data.players_shared[id]->PID = game_data.players[id].PID;
                    game_data.players_shared[id]->server_PID = game_data.players[id].server_PID;
                    game_data.players_shared[id]->type = game_data.players[id].type;
                    game_data.players_shared[id]->coords.x = x;
                    game_data.players_shared[id]->coords.y = y;
                    game_data.players_shared[id]->round_counter = game_data.players[id].round_counter;
                    game_data.players_shared[id]->direction = STAY;
                    game_data.players_shared[id]->slowed_down = 0;
                    game_data.players_shared[id]->deaths = 0;
                    game_data.players_shared[id]->coins_carried = 0;
                    game_data.players_shared[id]->coins_brought = 0;

                    int local_x = (x - 2) < 0 ? 0 : (x - 2);
                    int local_y = (y - 2) < 0 ? 0 : (y - 2);
                    for (int i = local_y; i <= (y + 2) && i < ROWS; ++i)
                    {
                        for (int j = local_x; j <= (x + 2) && j < COLUMNS; ++j)
                        {
                            game_data.players_shared[id]->player_board[i][j] = board[i][j];

                            if (board[i][j] == 'A')
                            {
                                game_data.players[id].campsite.x = j;
                                game_data.players[id].campsite.y = i;
                            }
                        }
                    }
                    for (int i = 0; i < SIGHT; ++i)
                    {
                        for (int j = 0; j < SIGHT; ++j)
                        {
                            if ((y - 2 + i) < 0 || (x - 2 + j) < 0 || (y - 2 + i) >= ROWS || (x - 2 + j) >= COLUMNS) game_data.players_shared[id]->player_minimap[i][j] = ' ';
                            else game_data.players_shared[id]->player_minimap[i][j] = board[y - 2 + i][x - 2 + j];
                        }
                    }
                    game_data.players_shared[id]->campsite.x = game_data.players[id].campsite.x;
                    game_data.players_shared[id]->campsite.y = game_data.players[id].campsite.y;

                    sem_init(&game_data.players_shared[id]->player_moved, 1, 0);
                    sem_init(&game_data.players_shared[id]->player_continue, 1, 1);

                    // setting up lobby info and allowing the player to leave the lobby and start playing
                    game_data.lobby->queue[id].want_to = CONTINUE;
                    sem_post(&game_data.lobby->leave);
                    sem_post(&game_data.lobby->ask);
                    break;
                }
            }
        }
    }
}

void* player_out(void* none)
{
    while (1)
    {
        // waiting for someone to ask to exit the game
        sem_wait(&game_data.lobby->exited);

        if (game_data.players_counter < MAX_PLAYERS)
        {
            for (int id = 0; id < MAX_PLAYERS; ++id)
            {
                // looking for the exiting player
                if (game_data.lobby->queue[id].want_to == EXIT)
                {
                    // cleaning up semaphores and player's shm
                    sem_destroy(&game_data.players_shared[id]->player_moved);
                    sem_destroy(&game_data.players_shared[id]->player_continue);
                    int pid = game_data.players_shared[id]->PID;
                    munmap(game_data.players_shared[id], sizeof(struct player_data_t));
                    game_data.players_shared[id] = NULL;
                    player_pid_shm("unlink", pid);

                    // reseting a bit server's struct
                    game_data.players[id].PID = -1;

                    // setting up lobby info and allowing the player to leave the lobby and exit
                    game_data.lobby->queue[id].want_to = BE_EMPTY;
                    sem_post(&game_data.lobby->leave);
                    sem_post(&game_data.lobby->ask);
                    break;
                }
            }
        }
    }
}

void* rounds_up(void* none)
{
    // check whether any player rage quitted
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (game_data.players_shared[id] != NULL)
        {
            int pid = game_data.players_shared[id]->PID;
            // that looser quitted, clean up after them
            if (kill(pid, 0) != 0)
            {
                // cleaning up semaphores and player's shm
                sem_destroy(&game_data.players_shared[id]->player_moved);
                sem_destroy(&game_data.players_shared[id]->player_continue);
                munmap(game_data.players_shared[id], sizeof(struct player_data_t));
                game_data.players_shared[id] = NULL;
                player_pid_shm("unlink", pid);

                // reseting a bit server's struct
                game_data.players[id].PID = -1;

                // setting up lobby info
                game_data.lobby->queue[id].want_to = BE_EMPTY;
            }
        }
    }

    int moved[MAX_PLAYERS] = {0};
    // check every connected player's decision
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (game_data.players_shared[id] != NULL)
        {
            // player has moved!
            if (sem_trywait(&game_data.players_shared[id]->player_moved) == 0)
            {
                moved[id] = 1;
                int new_x = game_data.players[id].coords.x;
                int new_y = game_data.players[id].coords.y;
                if (game_data.players[id].slowed_down == 0)
                {
                    switch (game_data.players_shared[id]->direction)
                    {
                        case NORTH:
                            new_y -= 1;
                            if (new_y < 0 || board[new_y][new_x] == '|') new_y += 1;
                            break;
                        case EAST:
                            new_x += 1;
                            if (new_x >= COLUMNS || board[new_y][new_x] == '|') new_x -= 1;
                            break;
                        case SOUTH:
                            new_y += 1;
                            if (new_y >= ROWS || board[new_y][new_x] == '|') new_y -= 1;
                            break;
                        case WEST:
                            new_x -= 1;
                            if (new_x < 0 || board[new_y][new_x] == '|') new_x += 1;
                    }
                }
                game_data.players[id].coords.x = new_x;
                game_data.players[id].coords.y = new_y;
            }

            // coins and bush stuff
            if (moved[id])
            {
                switch(board[game_data.players[id].coords.y][game_data.players[id].coords.x])
                {
                    case 'c':
                        game_data.players[id].coins_carried += 1;
                        board[game_data.players[id].coords.y][game_data.players[id].coords.x] = ' ';
                        break;
                    case 't':
                        game_data.players[id].coins_carried += 10;
                        board[game_data.players[id].coords.y][game_data.players[id].coords.x] = ' ';
                        break;
                    case 'T':
                        game_data.players[id].coins_carried += 50;
                        board[game_data.players[id].coords.y][game_data.players[id].coords.x] = ' ';
                        break;
                    case 'D':
                        game_data.players[id].coins_carried += game_data.chests[game_data.players[id].coords.y][game_data.players[id].coords.x];
                        board[game_data.players[id].coords.y][game_data.players[id].coords.x] = ' ';
                        game_data.chests[game_data.players[id].coords.y][game_data.players[id].coords.x] = 0;
                        break;
                    case 'A':
                        game_data.players[id].coins_brought += game_data.players[id].coins_carried;
                        game_data.players[id].coins_carried = 0;
                        break;
                    case '#':
                        game_data.players[id].slowed_down = !game_data.players[id].slowed_down;
                        break;
                }
            }
            game_data.players[id].direction = STAY;
            game_data.players[id].round_counter = game_data.round_counter;
        }
    }

    // looking for collisions with other players
    int dead[MAX_PLAYERS] = {0};
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        for (int id_enemy = 0; id_enemy < MAX_PLAYERS; ++id_enemy)
        {
            if (game_data.players_shared[id] != NULL && game_data.players_shared[id_enemy] != NULL && id_enemy != id)
            {
                if ((game_data.players[id_enemy].coords.x == game_data.players[id].coords.x) && (game_data.players[id_enemy].coords.y == game_data.players[id].coords.y))
                {
                    dead[id] = 1;
                    board[game_data.players[id].coords.y][game_data.players[id].coords.x] = 'D';
                    game_data.chests[game_data.players[id].coords.y][game_data.players[id].coords.x] += game_data.players[id].coins_carried;
                }
            }
        }
    }

    // looking for collisions with wild beasts
    for (int id_beast = 0; id_beast < MAX_BEASTS; ++id_beast)
    {
        if (game_data.beasts[id_beast].coords.x >= 0)
        {
            sem_post(&game_data.beasts[id_beast].go);
            sem_wait(&game_data.beasts[id_beast].done);
            for (int id = 0; id < MAX_PLAYERS; ++id)
            {
                if (game_data.players_shared[id] != NULL)
                {
                    if ((game_data.beasts[id_beast].coords.x == game_data.players[id].coords.x) && (game_data.beasts[id_beast].coords.y == game_data.players[id].coords.y))
                    {
                        // kill the player
                        dead[id] = 1;
                        board[game_data.players[id].coords.y][game_data.players[id].coords.x] = 'D';
                        game_data.chests[game_data.players[id].coords.y][game_data.players[id].coords.x] += game_data.players[id].coins_carried;

                        // kill the beast
                        pthread_cancel(game_data.beasts[id_beast].beast_thread);
                        game_data.beasts[id_beast].coords.x = -2;
                        game_data.beasts[id_beast].coords.y = -2;
                        sem_destroy(&game_data.beasts[id_beast].go);
                        sem_destroy(&game_data.beasts[id_beast].done);
                        break;
                    }
                }
            }
        }
    }

    // reset stats of the dead ones
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (dead[id])
        {
            int x = -1, y = -1;
            while (1)
            {
                x = rand() % COLUMNS;
                y = rand() % ROWS;
                if (FREE_SPACE(board[y][x])) break;
            }
            game_data.players[id].coords.x = x;
            game_data.players[id].coords.y = y;

            game_data.players[id].slowed_down = 0;
            game_data.players[id].deaths++;
            game_data.players[id].coins_carried = 0;
        }
    }

    // wrap up the round
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (game_data.players_shared[id] != NULL)
        {
            // update player's shm
            // (mini)map
            int x = game_data.players[id].coords.x;
            int y = game_data.players[id].coords.y;
            int local_x = (x - 2) < 0 ? 0 : (x - 2);
            int local_y = (y - 2) < 0 ? 0 : (y - 2);
            for (int i = local_y; i <= (y + 2) && i < ROWS; ++i)
            {
                for (int j = local_x; j <= (x + 2) && j < COLUMNS; ++j)
                {
                    game_data.players_shared[id]->player_board[i][j] = board[i][j];

                    // campsite spotted?
                    if (board[i][j] == 'A')
                    {
                        game_data.players[id].campsite.x = j;
                        game_data.players[id].campsite.y = i;
                    }
                }
            }
            for (int i = 0; i < SIGHT; ++i)
            {
                for (int j = 0; j < SIGHT; ++j)
                {
                    if ((y - 2 + i) < 0 || (x - 2 + j) < 0 || (y - 2 + i) >= ROWS || (x - 2 + j) >= COLUMNS) game_data.players_shared[id]->player_minimap[i][j] = ' ';
                    else game_data.players_shared[id]->player_minimap[i][j] = board[y - 2 + i][x - 2 + j];

                    // enemy players in sight?
                    for (int id_enemy = 0; id_enemy < MAX_PLAYERS; ++id_enemy)
                    {
                        if (game_data.players_shared[id_enemy] != NULL && id_enemy != id)
                        {
                            if ((game_data.players[id_enemy].coords.x == (x - 2 + j)) && (game_data.players[id_enemy].coords.y == (y - 2 + i)))
                            {
                                game_data.players_shared[id]->player_minimap[i][j] = id_enemy + 1 + '0';
                            }
                        }
                    }

                    // wild beasts in sight?
                    for (int id_beast = 0; id_beast < MAX_BEASTS; ++id_beast)
                    {
                        if (game_data.beasts[id_beast].coords.x >= 0)
                        {
                            if ((game_data.beasts[id_beast].coords.x == (x - 2 + j)) && (game_data.beasts[id_beast].coords.y == (y - 2 + i)))
                            {
                                game_data.players_shared[id]->player_minimap[i][j] = '*';
                            }
                        }
                    }
                }
            }
            game_data.players_shared[id]->campsite.x = game_data.players[id].campsite.x;
            game_data.players_shared[id]->campsite.y = game_data.players[id].campsite.y;

            // info
            game_data.players_shared[id]->coords.x = game_data.players[id].coords.x;
            game_data.players_shared[id]->coords.y = game_data.players[id].coords.y;
            game_data.players_shared[id]->round_counter = game_data.players[id].round_counter;
            game_data.players_shared[id]->direction = game_data.players[id].direction;
            game_data.players_shared[id]->slowed_down = game_data.players[id].slowed_down;
            game_data.players_shared[id]->deaths = game_data.players[id].deaths;
            game_data.players_shared[id]->coins_carried = game_data.players[id].coins_carried;
            game_data.players_shared[id]->coins_brought = game_data.players[id].coins_brought;

            if (moved[id]) sem_post(&game_data.players_shared[id]->player_continue);

        }
    }

    return NULL;
}

int draw_line_low(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int yi = 1;
    if (dy < 0)
    {
        yi = -1;
        dy = -1 * dy;
    }
    int D = 2*dy - dx;
    int y = y0;

    for (int x = x0; x < x1; ++x)
    {
        if (board[y][x] == '|') return 0;
        if (D > 0)
        {
            y = y + yi;
            D = D - 2*dx;
        }
        D = D + 2*dy;
    }

    return 1;
}

int draw_line_high(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int xi = 1;
    if (dx < 0)
    {
        xi = -1;
        dx = -1 * dx;
    }
    int D = 2*dx - dy;
    int x = x0;

    for (int y = y0; y < y1; ++y)
    {
        if (board[y][x] == '|') return 0;
        if (D > 0)
        {
            x = x + xi;
            D = D - 2*dy;
        }
        D = D + 2*dx;
    }

    return 1;
}

//int draw_line_straight(int x0, int y0, int x1, int y1, int id)
//{
//    int i = 1;
//    if (x0 == x1)
//    {
//        game_data.beasts[id].direction[1] = STAY;
//        if (y1 < y0)
//        {
//            i = -1;
//            game_data.beasts[id].direction[0] = NORTH;
//        }
//        else
//        {
//            game_data.beasts[id].direction[0] = SOUTH;
//        }
//        for (int y = y0; y < y1; y += i)
//        {
//            if (! FREE_SPACE(board[y][x0])) return 0;
//        }
//    }
//    else
//    {
//        game_data.beasts[id].direction[0] = STAY;
//        if (x1 < x0)
//        {
//            i = -1;
//            game_data.beasts[id].direction[1] = EAST;
//        }
//        else
//        {
//            game_data.beasts[id].direction[1] = WEST;
//        }
//        for (int x = x0; x < x1; x += i)
//        {
//            if (! FREE_SPACE(board[y0][x])) return 0;
//        }
//    }
//
//    return 1;
//}

int draw_line(int x0, int y0, int x1, int y1, int id)
{
    // it's.. complicated.
    if (y0 > y1) game_data.beasts[id].direction[0] = NORTH;
    else game_data.beasts[id].direction[0] = SOUTH;
    if (x0 > x1) game_data.beasts[id].direction[1] = WEST;
    else game_data.beasts[id].direction[1] = EAST;

    if (x0 == x1) game_data.beasts[id].direction[1] = STAY;
    if (y0 == y1) game_data.beasts[id].direction[0] = STAY;

    if (abs(y1 - y0) < abs(x1 - x0))
    {
        if (x0 > x1)
        {
            return draw_line_low(x1, y1, x0, y0);
        }
        else
        {
            return draw_line_low(x0, y0, x1, y1);
        }
    }
    else
    {
        if (y0 > y1)
        {
            return draw_line_high(x1, y1, x0, y0);
        }
        else
        {
            return draw_line_high(x0, y0, x1, y1);
        }
    }
}

void* beast_action(void* id)
{
    int id_beast = *(int *)id;

    while (1)
    {
        sem_wait(&game_data.beasts[id_beast].go);
        game_data.beasts[id_beast].direction[0] = STAY;
        game_data.beasts[id_beast].direction[1] = STAY;

        int beast_x = game_data.beasts[id_beast].coords.x;
        int beast_y = game_data.beasts[id_beast].coords.y;

        int moved = 0;
        for (int i = 0; i < SIGHT && moved == 0; ++i)
        {
            for (int j = 0; j < SIGHT && moved == 0; ++j)
            {
                // enemy players in possible sight?
                for (int id = 0; id < MAX_PLAYERS; ++id)
                {
                    if (game_data.players_shared[id] != NULL)
                    {
                        if ((game_data.players[id].coords.x == (beast_x - 2 + j)) && (game_data.players[id].coords.y == (beast_y - 2 + i)))
                        {
                            // try to draw a line (using Bresenham's line algorithm)
                            // returns 1 - you see them, chase them!
                            // returns 0 - eee what player? no idea what you're talking about

                            int player_x = game_data.players[id].coords.x;
                            int player_y = game_data.players[id].coords.y;

                            if (draw_line(beast_x, beast_y, player_x, player_y, id_beast) == 1)
                            {
                                // try to go in one of desired directions
                                switch(game_data.beasts[id_beast].direction[0])
                                {
                                    case NORTH:
                                        if (FREE_SPACE(board[beast_y-1][beast_x]))
                                        {
                                            moved = 1;
                                            beast_y -= 1;
                                        }
                                        break;
                                    case SOUTH:
                                        if (FREE_SPACE(board[beast_y+1][beast_x]))
                                        {
                                            moved = 1;
                                            beast_y += 1;
                                        }
                                        break;
                                }
                                if (moved == 0)
                                {
                                    switch(game_data.beasts[id_beast].direction[1])
                                    {
                                        case WEST:
                                            if (FREE_SPACE(board[beast_y][beast_x-1]))
                                            {
                                                moved = 1;
                                                beast_x -= 1;
                                            }
                                            break;
                                        case EAST:
                                            if (FREE_SPACE(board[beast_y][beast_x+1]))
                                            {
                                                moved = 1;
                                                beast_x += 1;
                                            }
                                            break;
                                    }
                                }
                            }
                        }
                    }
                    if (moved == 1) break;
                }
            }
        }
        if (moved == 0)
        {
            //wander around
            game_data.beasts[id_beast].direction[0] = rand() % 4 + 1;
            switch(game_data.beasts[id_beast].direction[0])
            {
                case NORTH:
                    if (board[beast_y-1][beast_x] != '|')
                    {
                        beast_y -= 1;
                    }
                    break;
                case SOUTH:
                    if (board[beast_y+1][beast_x] != '|')
                    {
                        beast_y += 1;
                    }
                    break;
                case WEST:
                    if (board[beast_y][beast_x-1] != '|')
                    {
                        beast_x -= 1;
                    }
                    break;
                case EAST:
                    if (board[beast_y][beast_x+1] != '|')
                    {
                        beast_x += 1;
                    }
                    break;
            }
        }

        game_data.beasts[id_beast].coords.x = beast_x;
        game_data.beasts[id_beast].coords.y = beast_y;

        sem_post(&game_data.beasts[id_beast].done);
    }
}
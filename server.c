#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <semaphore.h>
#include <fcntl.h>
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
#define MAX_CHESTS 100
#define SIGHT 5
enum type_t { HUMAN, CPU };
enum directions_t { STAY, NORTH, EAST, SOUTH, WEST };

struct coords_t
{
    int x;
    int y;
};

struct dropped_chests_t
{
    struct coords_t coords;
    int amount;
};

struct player_data_t
{
    int PID;
    enum type_t type;
    struct coords_t coords;
    enum directions_t direction;
    int slowed_down;
    int deaths;
    int coins_carried;
    int coins_brought;
    char player_minimap[SIGHT][SIGHT];
    char player_board[ROWS][COLUMNS];
};

struct game_data_t
{
    int PID;
    int round_counter;
    struct coords_t campsite;

    int players_counter;
    struct player_data_t* players[MAX_PLAYERS];

    int beasts_counter;
    struct coords_t beasts[MAX_BEASTS];

    int chests_counter;
    struct dropped_chests_t chests[MAX_CHESTS];

} game_data;

// semaphores
// 4 semafory z danymi do druku




int load_board(char *filename);
void set_up_game(int PID);
void* print_board(void* none);
void* key_events(void *none);

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

    pthread_t printing_thread;
    pthread_create(&printing_thread, NULL, print_board, NULL);

    usleep(1000000*MS);
    return 0;
}

void set_up_game(int PID)
{
    memset(&game_data, 0, sizeof(struct game_data_t));
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
        game_data.beasts[i].x = -2;
        game_data.beasts[i].y = -2;
    }
    for (int i = 0; i < MAX_CHESTS; ++i)
    {
        game_data.chests[i].coords.x = -2;
        game_data.chests[i].coords.y = -2;
        game_data.chests[i].amount = 0;
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
        // board
        for (int i = 0; i < ROWS; ++i)
        {
            for (int j = 0; j < COLUMNS; ++j)
            {
                // players and beasts

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

        // server info
        int cur_row = 0, cur_col = COLUMNS + 4;
        attron(COLOR_PAIR(C_DEFAULT));
        mvprintw(++cur_row, cur_col, "Server's PID: %d", -1);
        mvprintw(++cur_row, cur_col + 1, "Campsite X/Y: %d/%d", -1, -1);
        mvprintw(++cur_row, cur_col + 1, "Round number: %d", -1);

        // players info
        ++cur_row;
        for (int id = 0; id < MAX_PLAYERS; ++id)
        {
            int player_connected = 0;
            // if polaczony zmieniaj zmienna
            mvprintw(++cur_row, cur_col, "Parameter:");
            mvprintw(cur_row, cur_col + 13 + (id * 10), "Player%d", id + 1);
            mvprintw(++cur_row, cur_col + 1, "PID");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d", -1);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c", '-');
            mvprintw(++cur_row, cur_col + 1, "Type");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%s", "NONE");
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c", '-');
            mvprintw(++cur_row, cur_col + 1, "Curr X/Y");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d/%d", -1, -1);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%s", "--/--");
            mvprintw(++cur_row, cur_col + 1, "Deaths");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d", -1);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c", '-');
            mvprintw(++cur_row, cur_col + 1, "Coins");
            mvprintw(++cur_row, cur_col + 2, "carried");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d", -1);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c", '-');
            mvprintw(++cur_row, cur_col + 2, "brought");
            if (player_connected) mvprintw(cur_row, cur_col + 13 + (id * 10), "%d", -1);
            else mvprintw(cur_row, cur_col + 13 + (id * 10), "%c", '-');
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

        refresh();
        usleep(500 * MS);
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
                //postnij semafor zamykajacy gre
                return NULL;
            // add a new beast
            case 'b':
            case 'B':
                //watek dodania bestii
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
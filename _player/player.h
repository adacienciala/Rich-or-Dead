#define MS 1000

// colors stuff
#define C_PLAYER 1
#define C_WALL 2
#define C_BEAST 3
#define C_MONEY 4
#define C_CAMPSITE 5
#define C_DROPPED 6
#define C_DEFAULT 7
#define C_TERMINAL 8

// board stuff
#define ROWS 25
#define COLUMNS 51
#define FREE_SPACE(a) ( a == ' ' || a == '.' )

extern int max_x, max_y;

// game stuff
#define MAX_PLAYERS 4
#define SIGHT 5

enum type_t { HUMAN, CPU };
enum directions_t { STAY, NORTH, EAST, SOUTH, WEST };
enum state_t { BE_EMPTY, CONTINUE, JOIN, EXIT };

struct coords_t
{
    int x;
    int y;
};

struct player_data_t
{
    int ID;
    int PID;
    int server_PID;
    enum type_t type;
    struct coords_t coords;
    struct coords_t spawn_coords;
    struct coords_t campsite;
    int round_counter;
    enum directions_t direction;
    int slowed_down;
    int deaths;
    int coins_carried;
    int coins_brought;
    char player_minimap[SIGHT][SIGHT];
    char player_board[ROWS][COLUMNS];
    sem_t player_moved;
    sem_t player_continue;
} * player_data;

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
} * game_lobby;

void* key_events(void *none);
void* print_board(void* none);
int join_the_game(int pid);
int exit_the_game(int pid);
int player_pid_shm(char* action, int pid);

// player stuff
extern pthread_t print_board_thread, key_events_thread;
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

extern char board[ROWS][COLUMNS];
extern int free_spots;
extern int max_x, max_y;


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
    struct coords_t spawn_coords;
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

    struct player_data_t* players_shared[MAX_PLAYERS];
    struct player_data_t players[MAX_PLAYERS];

    struct beast_t beasts[MAX_BEASTS];
    int chests[ROWS][COLUMNS];

    pthread_mutex_t game_mutex;
} game_data;

// server's stuff
extern sem_t game_end;

int load_board(char *filename);
int set_up_game(int PID);
void* print_board(void* none);
void* key_events(void* none);
void* player_in(void* none);
void* player_out(void* none);
void* rounds_up(void* none);
int player_pid_shm(char* action, int pid);

int draw_line_low(int x0, int y0, int x1, int y1);
int draw_line_high(int x0, int y0, int x1, int y1);
int draw_line(int x0, int y0, int x1, int y1, int id);
void* beast_action(void* id);

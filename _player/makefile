CC	= gcc
LIBS	= -lcurses -pthread -lrt
ODIR	= obj
DEPS	= player.h 
_OBJ 	= main.o player.o 
OBJ 	= $(patsubst %,$(ODIR)/%,$(_OBJ))
TARGET	= player 

all: $(TARGET)

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p $(ODIR)
	$(CC) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) -o ../$@ $^ $(LIBS) 

clean:
	rm -rf $(ODIR) ../$(TARGET) 
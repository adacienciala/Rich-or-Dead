CC	= gcc
LIBS	= -lcurses -pthread -lrt -lm
ODIR	= obj
DEPS	= bot.h 
_OBJ 	= main.o bot.o 
OBJ 	= $(patsubst %,$(ODIR)/%,$(_OBJ))
TARGET	= bot 

all: $(TARGET)

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p $(ODIR)
	$(CC) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) -o ../$@ $^ $(LIBS) 

clean:
	rm -rf $(ODIR) ../$(TARGET) 
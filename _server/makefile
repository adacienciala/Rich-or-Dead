CC	= gcc
LIBS	= -lcurses -pthread -lrt
ODIR	= obj
DEPS	= server.h 
_OBJ 	= main.o server.o 
OBJ 	= $(patsubst %,$(ODIR)/%,$(_OBJ))
TARGET	= server 

all: $(TARGET)

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p $(ODIR)
	$(CC) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) -o ../$@ $^ $(LIBS) 

clean:
	rm -rf $(ODIR) ../$(TARGET) 
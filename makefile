all: server player bot

server: 
	$(MAKE) -C $(PWD)/_$@

player:
	$(MAKE) -C $(PWD)/_$@

bot:
	$(MAKE) -C $(PWD)/_$@

clean:
	$(MAKE) -C $(PWD)/_server clean
	$(MAKE) -C $(PWD)/_player clean
	$(MAKE) -C $(PWD)/_bot clean
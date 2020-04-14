# Rich or dead
Simple interprocess comunication game about the sad thing that life is. 

<div align="center">
  <img src="https://user-images.githubusercontent.com/57063056/79276702-c866b080-7ea8-11ea-9984-1d87af85d32e.gif" width="700"/>
</div>

## About
As one of the (maximum) 4 players - human or CPU - collect coins and deliver them to the campsite. Try to not encounter wild beasts or other participants - they will kill you and you'll drop your carried coins on the ground.

The game consists of 3 parts:
* game's server
* player's client
* bot's client

Communication is established thanks to **semaphores** and **shared memory** mechanisms. It's impossible to get information about the game's full map or any other vulnerable information from the player's perspective. The program uses multithreading. 

## Requirements and installation

To run the game, you need to have ncurses library installed.
```
sudo apt-get install libncurses5-dev libncursesw5-dev
```

Then, use the provided makefile to make *(sick!)* executable files.
```
make
```

## Server
<div align="center">
  <img src="https://user-images.githubusercontent.com/57063056/79276699-c7358380-7ea8-11ea-99e7-d65da40594ea.png" width="700"/>
</div>

### Running
```
./server
```

### Description
As a server, you have control over the map. 
* **c** - spawn coin (1 coin)
* **t/T** - spawn treasure (10 coins / 50 coins)
* **b/B** - spawn a wild beast
* **q** - close the game

If you quit the program by **ctrl+c**, shared memory files are gonna hang around. You can get rid of them simply by deleting them or running the server again and quitting with q.

## Player/Bot
<div align="center">
  <img src="https://user-images.githubusercontent.com/57063056/79276690-c56bc000-7ea8-11ea-998f-e41c774de8ab.gif" width="450"/>
  <img src="https://user-images.githubusercontent.com/57063056/79276694-c6045680-7ea8-11ea-80f8-639cfbfd0a34.gif" width="450"/>
</div> 

### Running the player
```
./player
```
<div align="center">
  <img src="https://user-images.githubusercontent.com/57063056/79276698-c7358380-7ea8-11ea-8bd6-53236dcf64bb.png" width="700"/>
</div> 

### Running the bot
```
./bot
```
<div align="center">
  <img src="https://user-images.githubusercontent.com/57063056/79276696-c69ced00-7ea8-11ea-9e1b-f0043df941d7.png" width="700"/>
</div> 

### Description 
The player has a limited **sight** - you're in the middle of a 5x5 square view. Move with the arrows and collect the coins. The **bushes** will slow you down. Watch out for the **beasts** - they can see and sense you from a great distance. If you don't feel like playing, use a bot.

Quit by pressing **q** or **ctrl+c** - the server will take care of your shms. 

## Notes
Things I learned thanks to this project: 
* [x] IPC mechanisms
* [x] multithreading
* [x] makefile
* [ ] inventing an algorithm that is not gonna allow the beast to sense you through walls

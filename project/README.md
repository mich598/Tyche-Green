How to Use Shell Commands

west build -b disco_l475_iot1 --pristine

**new round** = signals new round for player. Can start betting here
**new player** = signals for camera to process new player card 
**new dealer** = signals for camera to process new dealer card

File system allows player to track which cards they have recieved. The card value is not displayed on the dashboard since it ruins the point of the game. 

**logging ls** = access all directories
**logging w filename** = write the player's current card into the file
**logging r filename** = read the player's card from the given file

EXAMPLE IMPLEMENTATION
new player
logging w cards (cards is file name) 
logging r cards
logging ls

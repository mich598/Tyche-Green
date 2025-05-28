How to Use Shell Commands

<west build -b disco_l475_iot1 --pristine>

<new round> = signals new round for player. Can start betting here
<new card> = signals for to process camera input. Type in after card is recieved on Sensor node 

File system allows player to track which cards they have recieved. The card value is not displayed on the dashboard since it ruins the point of the game. 

<logging ls> = access all directories
<logging w filename> = write the player's current card into the file
e.g. logging w cards

<logging r filename> = read the player's card from the given file
e.g. logging r cards
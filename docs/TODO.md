# Potential game evolution

## Performance Optimization

### Add Base Distance Field

Use BFS / flood fill starting from the base. Use multi-source flood fill (or multi-source BFS). If enemies need to know which base you store an additional field.

Drones are consumers of this data. Helps to stay close to the base, closer to optimal distance, defined by const.

### Add Player Distance Field

May be used for enemy AI.

For example:

- if (distance < 10) attack
- if (distance > 30) patrol

## Visuals

- Make Explosions sprites 1 bit

## Cheap tune music

### Bytebeat

[](https://dollchan.net/bytebeat/#4AAAA+kXTqCuxszPS1NLQMDQyVyvR0gASdnaGBpqaNhpGJqYgESNtDVOwoIkmEAAA)
[](https://dollchan.net/bytebeat/#4AQBELEdliz0Lg0AMhn+NkIToebmrdYm7m7s42EE4lEKp4Af++F5rp3Z4P3iSd1BYeVHHm3reVfihVkruddTqGe4wUlMbj6gVBF3Ng0ftb08IKYQjRyTaaeP5OGDSHHnSPi4WJAiJTS0iD7DobDy1loVPOZbsGuX5lzl2mZyXb7qsiL388E9Gv/x718JcVdZh4qQjG4eQXU1T02xsLj7S1BZI8v5mucRa+Bc)

## Version 2 Roadmap Ideas

### Drones Starting the Alarm

Drones can start the alarm, making other enemies know player location. It may create stealth element to the game!


# Potential game evolution

## Fixes

- Use a more appropriate sound for a projectile hitting a wall
- Find an alarm sound effect
- The alarm doesnt affect gameplay significantly

## Visuals

- Make Explosions sprites 1 bit
- Use sprite for a broken base

## Performance Optimization

Monitor performance

## Improvements and Features

- maze generation parameters, like in my react app

```typescript
export type GeneratorParams = {
  width: number;
  height: number;
  cohesion: number;
  direction: WindDirection;
  strength: number;
  density: number;
};
```

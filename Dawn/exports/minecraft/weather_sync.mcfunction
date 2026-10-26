execute if score dawn_weather state matches 0 run weather clear 60
execute if score dawn_weather state matches 1 run weather rain 60
execute if score dawn_weather state matches 2 run effect give @a minecraft:poison 1 0 true
execute if score dawn_weather state matches 3 run weather thunder 60

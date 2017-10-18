import matplotlib.pyplot as plt, matplotlib.cm as cm
import bezier
import numpy

#speed of pod
MACH1 = 343 #m/s

# The solution points for the bezier curve
sol = [[0,0],[8405.97,35422.5],[44262.6,42212.5],[30469.3,59396.3],[73932.9,44899.1],[77440.6,76022],[71098.9,62165.4],[70000,80000],]

#define the bezier curve
curve = bezier.Curve(numpy.array(sol), len(sol) - 1)

#calculate the length of the bezier curve
arcLength = curve.length

#calculate the time to travel the length of the tube
time = (arcLength / (.95 * MACH1)) / 60 

#define the current plot
currentPlot = curve.plot(3700)

#create the string to be overlayed on the graph
timeString = "Time to travel the route at Mach1 in minutes: " + str(time)
distanceString = "Distance traveled in meters: " + str(arcLength)

#annotate the graph
currentPlot.annotate(timeString, xy=(7000,0), xytext=(7000,0))
currentPlot.annotate(distanceString, xy=(7000,4000), xytext=(7000,4000))

#output the graph to a file
plt.savefig("plot.png") 
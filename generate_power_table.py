from math import sin, pow, pi

samples = 2<<15
finalSamples = 100

power = [pow(sin(pi*a/samples),2) for a in range(samples)]
totalPower = 1.0
integral = [0 for a in range(samples)]
for i in range(samples):
    integral[i] = totalPower
    totalPower = totalPower - 2*(power[i]/samples)

aux = 1.0
iaux = 0
inverse = [0 for i in range(finalSamples)]
for i in range(samples):
    if integral[i]<aux:
        inverse[iaux] = samples-i
        iaux = iaux+1
        aux = aux-(1.0/finalSamples)

print(len(inverse))
print(inverse)

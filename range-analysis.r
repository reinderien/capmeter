#!/usr/bin/R -q --vanilla -f
options(echo=F)

library(ggplot2)
library(scales)


# Variable setup ###########################################################

# taus = t/RC
# t = timer*s/f

R = t(c(270, 15e3, 1e6))    # all drive resistors
colnames(R) = R
taustable = 7               # taus to stabilization
taufall = -log(1.1/5)       # taus from 5V to 1.1V
taurise = -log(1-1.1/5)     # taus from 0V to 1.1V     
f = 16e6                    # timer frequency
s = matrix(2^c(0,3,6,8,10)) # all prescaler factors
rownames(s) = s

# To choose the various scales, either we want to choose a
# minimum timer resolution and then optimize for fastest time,
# or choose a maximum time and then optimize for highest resolution.
# Here we're doing the latter.
tmax = 4e-6 * 2^16          # allows for nice range endings at a timer of 0xFFFF
timermax = pmin(tmax*f/s, 2^16-1)

# Based on the max time and timer, max caps for each R and s
Cmax = (timermax*s/f/taustable) %*% (1/R)

# Based on a timer count of 1, min caps for each R and s
Cmin = (s/f/taurise) %*% (1/R)

# various capacitors over the full range
C = matrix(10^seq(-14, -2, by=1/24))
rownames(C) = C  # sprintf('%.2e', C)

# The stabilization time is two-dimensional, over C and R.
tm = taustable*C %*% R

# The stabilization timer value is three-dimensional, over C, R, and s.
timer = drop((f*tm) %o% (1/s))
tmrcompare = aperm(drop(replicate(length(C),
                                  replicate(length(R), timermax))),
                   c(3,2,1))
timer[timer>tmrcompare | timer<1] = NA

# To perform actual range selection:
# There is only one range selected for every C.
# For each C, select the R and s for which:
#   t < tmax
#   1 <= timer < 2^16
#   timer is maximal
# critical points are for each C where:
#   timer crosses 1 or 2^16
#   t crosses tmax
# 6 = t/RC = tim*s/RC/16MHz
# C = t/6R = tim*s/6R/16MHz
# Ccrit = tmax/6R = 2^16*s/6R/16MHz
crit = expand.grid(R=R,s=s)
Ccrit = with(crit, c(s/f/R, 2^16*s/f/R, tmax/R))/taustable
crit = expand.grid(R=R, s=s, C=Ccrit)
crit$t = with(crit, taustable*R*C)
crit$tmr = with(crit, t*f/s)
eps=1e-6  # epsilon tolerance required to accommodate for float error
# Only keep conformant rows.
crit = crit[crit$tmr>=1-eps &
            crit$tmr<=2^16+eps &
            (crit$t<=tmax+eps | crit$R==R[1]),]
# Suitability of every range at a certain C is determined mainly by the highest
# timer but also by the lowest time.
crit = crit[with(crit, order(-C, -tmr^3/t)),]
# We now need pairs of R&s: the first in each pair being either the extreme
# lowest C or the C where the previous range cut out; and the second being the
# maximum condition.
ranges = tail(crit, 1)
samec = ranges
repeat {
   # Find the furthest row with the same R and s
   edge = crit[which(crit$R==samec$R & crit$s==samec$s)[1],]
   ranges = rbind(ranges, edge)
   # Find the (different, conformant) row with the same C and the highest timer
   samec = crit[which(crit$C == edge$C &
                      crit$tmr < 2^16-eps &
                      crit$R <= edge$R &
                      (crit$t < tmax-eps | crit$R==R[1])),][1,]
   if (is.na(samec$R)) break
   ranges = rbind(ranges, samec)
}
rownames(ranges) = 1:(nrow(ranges))
print(ranges)


# Plot utilities ###########################################################

# Adapted from https://stackoverflow.com/questions/30179442
log_breaks = function(maj, radix=10) {
  function(x) {
    minx = floor(min(logb(x, radix), na.rm=T)) - 1
    maxx = ceiling(max(logb(x, radix), na.rm=T)) + 1
    n_major = maxx - minx + 1
    major_breaks = minx:maxx
    if (maj) {
      breaks = major_breaks
    } else {
      breaks = rep(logb(1:(radix-1), radix), times=n_major) +
               rep(major_breaks, each=radix-1)
    }
    radix^breaks
  }
}
# See https://github.com/tidyverse/ggplot2/blob/master/R/scale-continuous.r#L160
scale_x_log_eng = function(..., radix=10) {
  scale_x_continuous(..., trans=log_trans(radix),
                     breaks=log_breaks(T, radix),
                     minor_breaks=log_breaks(F, radix))
}
scale_y_log_eng = function(..., radix=10) {
  scale_y_continuous(..., trans=log_trans(radix),
                     breaks=log_breaks(T, radix),
                     minor_breaks=log_breaks(F, radix))
}
xaxis_text_vert = function() {
  theme(axis.text.x=element_text(angle=90))
}


# Plotting #################################################################

tm_df = as.data.frame.table(tm)
colnames(tm_df) = c('C', 'R', 't')
tm_df$C = as.numeric(levels(tm_df$C))[tm_df$C]
ggplot(tm_df, aes(x=C, y=t)) +
   ggtitle(bquote(paste('Stabilisation time against R and C for ',
                        t/tau == .(taustable)))) +
   geom_line(aes(colour=R, group=R)) +
   geom_hline(aes(yintercept=tmax)) +
   geom_text(data=data.frame(), size=3,
             aes(x=1e-14, y=tmax, label=paste('max=', tmax),
                 hjust='left', vjust=-0.5)) +
   scale_x_log_eng() + scale_y_log_eng() + xaxis_text_vert()


tmr_df = as.data.frame.table(timer)
colnames(tmr_df) = c('C', 'R', 's', 'timer')
tmr_df$C = as.numeric(levels(tmr_df$C))[tmr_df$C]

maxdf = data.frame(timer=timermax, s=factor(s))
maxnames = data.frame(C=1e-14, s=factor(s), timer=timermax)

ggplot(tmr_df, aes(x=C, y=timer, linetype=s)) +
   ggtitle(bquote(paste('Timer against prescaler, R and C for ',
                        t/tau == .(taustable)))) +
   geom_line(aes(colour=R, group=interaction(R,s))) +
   geom_hline(data=maxdf, aes(yintercept=timer, linetype=s, group=s)) +
   geom_text(data=maxnames, size=3,
      aes(label=paste('max=',timer), hjust='left', vjust=-0.5, group=s)) +
   scale_x_log_eng() + xaxis_text_vert() +
   scale_y_log_eng(radix=4, limits=2^c(0,16))


# At the bottom end, cut off at typical parasitic capacitance
ranges[ranges$tmr==1,] = data.frame(R=1e6, s=1, C=50e-12,
                                    t=taustable*1e6*50e-12,
                                    tmr=taustable*1e6*50e-12*f)
ranges$s = factor(ranges$s)
ranges$R = factor(ranges$R)
ggplot(ranges, aes(x=C, y=tmr, colour=R, linetype=s,
                   group=interaction(R,s))) +
   ggtitle(bquote(paste('Timer against prescaler, R and C for ',
                        t/tau == .(taustable), ' (chosen ranges)'))) +
   geom_line() +
   scale_x_log_eng() + xaxis_text_vert() +
   scale_y_log_eng(radix=2)


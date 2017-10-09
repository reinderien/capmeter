#!/usr/bin/R -q -f

library(ggplot2)

# taus = t/RC
# t = timer*s/f

R = t(c(270, 10e3, 1e6))    # all drive resistors
colnames(R) = R
taustable = 6               # taus to stabilization
taufall = -log(1.1/5)       # taus from 5V to 1.1V
taurise = -log(1-1.1/5)     # taus from 0V to 1.1V     
f = 16e6                    # timer frequency
s = matrix(2^c(0,3,6,8,10)) # all prescaler factors
rownames(s) = s

# To choose the various scales, either we want to choose a
# minimum timer resolution and then optimize for fastest time,
# or choose a maximum time and then optimize for highest resolution.
tmax = 0.5

# Based on the max time, max caps for each R
Cmax_time = tmax/taustable/R
Cmax_time_compare = t(replicate(length(s), as.vector(Cmax_time)))

# Based on the max timer, max caps for each R and s
Cmax_timer = ((2^16 - 1)*s/f/taustable) %*% (1/R)

Cmax = pmin(Cmax_timer, Cmax_time_compare)

# Based on a timer count of 1, min caps for each R and s
Cmin = (s/f/taurise) %*% (1/R)

# various E12 capacitors over the full range
C = matrix(10^seq(-12, -3, by=1/12))
rownames(C) = C  # sprintf('%.2e', C)

# The stabilization time is two-dimensional, over C and R.
tm = taustable*C %*% R

# The stabilization timer value is three-dimensional, over C, R, and s.
timer = drop((f*tm) %o% (1/s))

# For every C there is a one-dimensional array of times and a
# two-dimensional array of timer values.



# Adapted from https://stackoverflow.com/questions/30179442
log10_breaks = function(maj) {
  function(x) {
    minx         = floor(min(log10(x), na.rm=T)) - 1
    maxx         = ceiling(max(log10(x), na.rm=T)) + 1
    n_major      = maxx - minx + 1
    major_breaks = seq(minx, maxx, by=1)
    if (maj==TRUE) breaks = major_breaks
    else breaks = rep(log10(seq(1, 9, by=1)), times=n_major) +
                  rep(major_breaks, each=9)
    10^breaks
  }
}
scale_x_log10_eng = function(...) {
  scale_x_log10(..., breaks=log10_breaks(TRUE),
                     minor_breaks=log10_breaks(FALSE))
}
scale_y_log10_eng = function(...) {
  scale_y_log10(..., breaks=log10_breaks(TRUE),
                     minor_breaks=log10_breaks(FALSE))
}

tm_df = as.data.frame.table(tm)
colnames(tm_df) = c('C', 'R', 't')
tm_df$C = as.numeric(levels(tm_df$C))[tm_df$C]
ggplot(tm_df, aes(x=C, y=t, colour=R, group=R)) +
   ggtitle(bquote(paste(
      'Stabilisation time against R and C for ',
      t/tau == .(taustable)))) +
   geom_line() +
   scale_x_log10_eng() + scale_y_log10_eng() +
   theme(axis.text.x=element_text(angle=90))

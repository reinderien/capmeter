#!/usr/bin/R -q --vanilla -f

library(ggplot2)
library(scales)


# Variable setup ###########################################################

# taus = t/RC
# t = timer*s/f

R = t(c(270, 10e3, 1e6))    # all drive resistors
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
tmax = 0.1
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
timer[timer<1 | timer >= 2^16] = NA


# Plot utilities ###########################################################

# Adapted from https://stackoverflow.com/questions/30179442
log_breaks = function(maj, radix=10) {
  function(x) {
    minx = floor(min(logb(x, radix), na.rm=T)) - 1
    maxx = ceiling(max(logb(x, radix), na.rm=T)) + 1
    n_major = maxx - minx + 1
    major_breaks = seq(minx, maxx, by=1)
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
                     breaks=log_breaks(TRUE, radix),
                     minor_breaks=log_breaks(FALSE, radix))
}
scale_y_log_eng = function(..., radix=10) {
  scale_y_continuous(..., trans=log_trans(radix),
                     breaks=log_breaks(TRUE, radix),
                     minor_breaks=log_breaks(FALSE, radix))
}


# Plotting #################################################################

tm_df = as.data.frame.table(tm)
colnames(tm_df) = c('C', 'R', 't')
tm_df$C = as.numeric(levels(tm_df$C))[tm_df$C]
ggplot(data=tm_df, aes(x=C, y=t)) +
   ggtitle(bquote(paste('Stabilisation time against R and C for ',
                        t/tau == .(taustable)))) +
   geom_line(aes(colour=R, group=R)) +
   geom_hline(aes(yintercept=tmax)) +
   geom_text(data=data.frame(), size=3,
      aes(x=1e-14, y=tmax, label=paste('max=', tmax),
          hjust='left', vjust=-0.5)) +
   scale_x_log_eng() + scale_y_log_eng() +
   theme(axis.text.x=element_text(angle=90))


tmr_df = as.data.frame.table(timer)
colnames(tmr_df) = c('C', 'R', 's', 'timer')
tmr_df$C = as.numeric(levels(tmr_df$C))[tmr_df$C]

maxdf = data.frame(timer=timermax, s=factor(s))
maxnames = data.frame(C=1e-14, s=factor(s), timer=timermax)

ggplot(data=tmr_df, aes(x=C, y=timer, linetype=s)) +
   ggtitle(bquote(paste('Timer against R and C for ',
                        t/tau == .(taustable)))) +
   geom_line(aes(colour=R, group=interaction(R,s))) +
   geom_hline(data=maxdf, aes(yintercept=timer, linetype=s, group=s)) +
   geom_text(data=maxnames, size=3,
      aes(label=paste('max=',timer), hjust='left', vjust=-0.5, group=s)) +
   scale_x_log_eng() +
   scale_y_log_eng(radix=4, limits=2^c(0,16)) +
   theme(axis.text.x=element_text(angle=90))

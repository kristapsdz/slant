
- Support compression in slant-http.c.

- Show more things: boottime, etc.

- Add multi-row support for extra information.

- Average last two time series instead of showing only the last.
  Just showing the last is confusing when a bucket restarts, as it will
only have the last data sample to show.

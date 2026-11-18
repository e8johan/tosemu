# The cross toolchain comes from Vincent Rivière's PPA. add-apt-repository
# fetches the PPA's signing key from Launchpad itself; the key that used to be
# added here with apt-key was another one, and apt-key is gone from the
# Ubuntu the runners now have.
sudo add-apt-repository -y ppa:vriviere/ppa

# Launchpad's file server answers 503 now and then, an index or a package at
# a time, and apt leaves out whatever it could not fetch - which reads as
# cross-mint-essential depending on a binutils that does not exist. So each
# download is tried again before it is given up on.
RETRY="-o Acquire::Retries=5"

sudo apt-get $RETRY update -ym
sudo apt-get $RETRY install -ym cross-mint-essential libpng-dev \
  libfreetype-dev libfontconfig-dev libasound2-dev libssl-dev libjpeg-dev \
  libwebp-dev

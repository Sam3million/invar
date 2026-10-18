BUILD_DIR="$1"
for i in 1 2 3 4 5; do .BUILD_DIR/invar --bench --label classic  --inFlight 1 --frames 1000 --res 2560x1440; done
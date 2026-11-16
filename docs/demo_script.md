# Demo script (about 5 minutes)

## Setup (before the demo)

```bash
./scripts/build.sh          # builds and tests the C++ engine
./scripts/run_demo.sh       # creates .venv if needed and opens Streamlit on port 8501
```

Open http://localhost:8501. The sidebar should show the engine version, for example
`flowsim_cli 1.0.0 (C++20, gcc 11.4, Release, schema 1)`.

## 1. Live demo (90 s)

The app opens on **Live demo**. Press **Load sample and simulate**. Two security halls start playing with the
same passengers: **Today** with 4 lanes on the left, the **FlowSim plan** with 6 lanes on the right.

- Let the audience watch the left line grow while the right hall keeps up. Rings under passengers turn amber and
  then red as they wait longer than the 10 minute target.
- Point at **Cost so far**: the plan costs a little more at first because it pays for 2 extra lanes, then the
  waiting and late passengers on the left overtake it around the 2 hour mark.
- Press **8x** to reach the end. The summary card shows the result for this run and the average saving over 10
  simulated runs (about 34%).

Use **Replay** or drag the timeline to show any moment again.

## 2. Scenario (30 s)

Go to **Scenario Builder**. Point out the illustrative morning peak: 240 minute window, 48 customers per hour,
5.5 minute mean service, current plan of 4 counters, costs of 250 per counter-hour and 35 per waiting
customer-hour, and a 10 minute SLA with a 20 unit penalty per breach. The caption shows the offered load
(4.4 busy counters on average), so 4 counters cannot keep up.

## 3. Simulate the current plan (60 s)

Go to **Simulation**, set 4 counters and press **Run simulation**. Walk through the KPI cards, then the
**Timeline** tab (each bar is one customer, highlighted bars waited beyond the SLA) and **Queue & utilization**
(queue grows through the window). Use **Playback** to scrub the clock and show which counters are busy.

## 4. Optimize (90 s)

Go to **Optimize Staffing** and press **Optimize staffing**. With seed 42 and 10 replications the engine
recommends **6 counters**: mean cost about 6,459 versus 9,787 for the current 4 counters (about 34% lower),
and mean wait falls from about 20 minutes to about 1.2 minutes. Read the explanation sentence: going to 5
saves 961 in staffing but adds 1,017 in waiting and penalties; going to 7 adds 1,018 in staffing to save 149.
Show the stacked cost chart (U-shaped total) and the representative queue comparison.

## 5. Experiments and export (45 s)

Go to **Experiments & Export**, press **Optimize with this seed** (seed 43) and show the run history with both
runs. Download the optimization CSV and JSON.

## 6. Fixed data (optional, 30 s)

On **Scenario Builder** switch to **Uploaded customer CSV**, press **Use sample CSV (synthetic data)** and
optimize again. Fixed data runs once per staffing level, so there is no replication spread.

## Close (15 s)

"The recommendation is the minimum-cost plan under these assumptions and cost weights. Real use needs
calibrated arrival and service data, and the next steps are time-varying arrivals and shift-based staffing."

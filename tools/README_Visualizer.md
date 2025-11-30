# Visualizer Operation

The '[viz](src/viz.py)' program is what allows a user to look inside a log and see what happened.
The general goal is to allow users to:

* Select the specific ECU data streams they are interested in examining
* Navigate through time to select the area of the log they are interested in
* Zoom in and out to see detail as required

At this point, it is best to just show some pictures.

Here is a log of me starting up on a fairly cold November day, letting the bike warm for a long time, then going for a short ride.

![overview](images/viz-1a.jpg)

## Data Streams

The pane on the left shows all the data streams that can be visualized.
In this example, we are looking at instantaneous RPM (red) and coolant temperature (blue).

As an engine rotates, the crank speeds up and slows down.
That's one of the reasons you feel vibration when the engine runs.
The tachometer on the dashboard is heavily filtered so that you do not see the RPMs rising and falling during a single rotation.
When trying to see details of engine operation though, we want to see how the speed varies during a rotation: the instantaneous RPM.
The definition of 'instantaneous' RPM is based on the fact that the Aprilia ECU tracks the position of the crankshaft 6 times per rotation (event 60 degrees).
The time period for each 60 degree subrotation can be converted into its 'instantaneous' rotational speed over that time.

## Navigation Window

The narrow strip at the bottom is called the Navigation View.
The navigation view shows the entire log (timewise).
In this case you can see that I warmed the bike up for nearly 600 seconds (10 minutes) before heading out.
The bike is clearly warming up as evidenced by the slowly rising blue line in the navigation window.
You can see the temp rise until it hits about 75C, which is where the thermostat opens.
As the thermostat opens, the temp holds at 75C until the coolant in the radiator warms up, and the temp starts climbing again.

You can see the revs rise twice as I left my driveway, then three more times as I drove down the road away from my house towards the main road.

I had to wait for at idle for nearly 90 seconds before traffic opened up and I could turn left onto the main road.
After that, revs rise and fall as I drive around.
As the bike gets some velocity under it and some cold air through its radiator, you can see the water temps drop right back to 75C again.
The thermostat does its job, and never lets the bike go below 75C though.

And at the very end, RPMs are back to idle as I get the bike parked in the garage.

## Details

The navigation window always shows the entire ride, but there will be plenty of times when detail is even more fun.

To get detail in a ride, look for that narrow blue box in the navigation window.
The data inside that blue box corresponds to what you see in the big graphic window.

You can drag the edges of the blue box to zoom in or out, or click and drag the blue box to look at different parts of the log.
That's why it is called the navigation window.

Let's look at that first graph in more detail:
![overview](images/viz-1a-annotated.jpg)

The blue arrow shows the engine turning slowly as the starter cranks it over.
Note: the blue and green arrows were added manually for the purposes of annotating the graph on this webpage - the visualizer did not put them in by itself.

After only a bit of cranking, you can see the engine fire up.
In about 2 rotations of the crankshaft, the RPMs jump from below 500 RPM to nearly 2000 RPM.
The RPMs picked up even more until I adjusted the handlebar fast idle control to slow it down a bit.

What is really interesting though is the 7 green arrows.
I put those in because as the bike started that day, it was missing.
Running rough.
The green arrows clearly show the engine missing - slowing down for a rotation or two, then speeding back up again to where the RPMs should have been all along.

And this is where the visualizer proves its worth.
Zooming in on that very first green arrow, we see this:
![detail](images/viz-detail-1.jpg)

You can see the rotation speed of the crank drop from nearly 2000 RPM to below 1000 RPM.

So what's going on here?
I'm glad you asked...

First off, lets take a look at a diagram from the Aprilia doc describing the sequence of events that occur as the engine operates:
![operation](../ecu/doc/OperationalSequencing.jpg)

The diagram shows a complete two-rotation sequence of what happens in the 4-stroke Aprilia engine.
The graph line N (NNUM) is what I call the CRID, or Crankshaft Reference identifier.
The CRID starts at 0, counts up to 11, then the CAM sensor tells it that CR0 is about to happen again, and it all starts over.
The important aspects of the Aprilia diagram show that the power stroke for the front cylinder starts on CR5 and runs through CR7.
For the rear, the power stroke starts on CR10 and runs through CR0.

I'm proud of this next bit: one of the graphical ECU streams I can display is the CRID, and I can display it in relation to instantaneous RPM:
![crid-detail](./images/viz-detail-crid.jpg)
Those markers indicate the start of each CR event, as per the Aprilia doc.

So what does it tell us?
Firstly: the power stroke may not be entirely intuitive.
Starting from the left side of that graph, you can see CR10,11, and 0 which represent the power stroke of the rear cylinder.
You can see that the crank barely speeds up at all during CR10, even though it represents the first 60 degrees of the power stroke.
Essentially all of the increase in speed of the crank occurs during CR11, the middle 60 degrees of the power stroke.
It actually makes sense: if you think about it.
When the piston is at the top of its stroke, the pressure in the cylinder might be really high, but the ability to turn that pressure into mechanical rotation is almost nothing because the rod itself is vertical.
The piston gains the most mechanical advantage to turn the crank after the crank has rotated 60 more degrees to begin CR11.
The final 60 degrees of the power stroke in CR0 is also pretty wimpy.
That's because the rod is becoming vertical again so there is little mechanical advantage to twist the crank, and temperature and pressure in the cylinder has dropped dramatically because the increase in volume in the cylinder as the piston sweeps downwards.

The takeaway is that when things are running properly, the engine will clearly increase speed during:

* CR6: when the front cylinder fires 
* CR11: when the rear cylinder fires.

Now check out the detail in the circled area below:
![operation](./images/viz-detail-2-crid.jpg)

There are a few things to note.
If you look inside the orange cicle, you see a proper firing cycle.
* All CRIDs 3/4/5 are about the same width
* They are all slowing though, which is correct because it has been some time since the last firing event
* CR6 shows the increase in engine RPM, which is exactly right because it is the maximally effective portion of the power stroke
* The engine genererally slows down after that until CR11 when the rear cylinder fires and speeds it up again

So what happened when the engine "missed", inside the blue circle?
A few things are obvious:

1) The duration of both CR4 and CR5 are a lot wider than CR3. That is NOT how the previous cycle CR4/5 looked where CR3/4/5 were essentially the same duration.
1) There is visible proof that engine did __not__ misfire: it clearly speeds up during CR6. Had it been a genuine misfire, CR7 would have shown an even lower RPM.
1) That CR5 took an extra long time is a surprise. This is the very initial part of the power stroke. Normally, you can see the crank slow during CR5 because it was just completing the compression stroke, and althought the spark has fired, it has no mechanical advantage yet for the rod to turn the crank.

So why was CR5 _so_ much slower?
Well, there is more data to look at.

The ECU has circuitry that detects when the coils fire.
It detects the high voltage kickback from each of the two ignition coils per cylinder when they fire and latches the time that the high voltage was observed occurred.
_Note: This is not to say that we can guarantee that a spark occurred: all we know is that the coil fired.
That said, it is pretty strong evidence that a spark occurred!_
The UM4 firmware in the ECU logs the time of every spark event.
Displaying that yields interesting results:
![operation](./images/viz-detail-3.jpg)

The tags marked 'S2' indicate when plug #2 fired.
The ECU also tracks when plug 1 fires, but the timing for plug 1 and 2 only differ by microseconds, so for simplicity, I only displayed sparks on plug #2 on this graph.

If you look at the first 3 spark firing events, you can see that they line up almost exactly with the start of CR10 or CR5, meaning that they are firing when the piston basically right at TDC (Top Dead Center).

But if you look at the problem event, you see something different.
In short, CR4 takes unusually long to complete, which has the side effect that the spark occurs significantly in advance of TDC.

And _that_ means that pressures in that cylinder are going to be extra high before the piston has even got to the top yet.
The extra high pressure will slow the crank down more than normal as it tries to get over the top to begin the power stroke.

The next spark timing event shows that it is a tiny ahead of CR10, but apparently not enough to cause a noticible problem.
The final spark event in that view is right where it should be.

The net result is that that the "misses" were not actually misses, but the result of a spark that fired a bit too early.

Why _that_ happened, I do not know yet.
There will always be mysteries.

There is more data, too.
Here is an example showing the cam sensor signal.
The cam sensor indicates that the next CRID will be CR0, as can be seen here:

![cam sensor operation](images/viz-detail-4.jpg)

## GPS Operation

The umod4 PCB also contains a Neo-8 GPS.
Position and velocity data are captured and correlated with ECU operation.
Much fun ensues!

I added a button to the visualizer to display all the GPS info from a ride using Google Maps.
This is what I got from that ride:

![ride-1-overview](images/ride-1-overview.jpg)

The level of detail is reasonably significant since the GPS reports 10 times per second:

![ride-1-detail](images/ride-1-detail.jpg)

Each of those blue dots is a position and velocity  report.

And now that I have GPS, even more debugging becomes possible.
Case in point: I went for another ride a day later.
I nearly froze my hands off.
But, as I was turning left by the Safeway store, the engine missed.
I made a mental note of that.
After getting home, I looked up my ride visualization.
It was a longer trip, hence the frozen fingers.
![ride-2](images/ride-2-overview.jpg)

I used Google Maps to zoom in and find that left turn by the Safeway:
![ride-2-safeways](images/ride-2-safeways.jpg)

Clicking on any of the blue dots gave me the time associated with that event (approx. 1889 seconds into the ride), and I could use value that to find the general area in the navigation view:
![ride-2-safeways](images/ride-2-safeways-detail.jpg)

From there, it was not hard to scan through the instantaneous RPM data and find the engine miss I had felt.
Above, you can see me sitting at the corner, throttle closed (green graph line).
Then you can see me feed in a bit of throttle, and accelerate through the corner.
The engine clearly misbehaves pretty much exactly where the green throttle line intersects with the red RPM trace at 1888.4 seconds.
Zooming in on that event makes things clear:
![ride-2-safeways](images/ride-2-safeways-detail-3.jpg)

Check out CR6 in the circled area: that is where the engine should speed up during the power stroke.
Instead the engine keeps slowing down.
You can see that the spark occured just before CR5, and the width of CR5 generally matches CR2/3/4, so this is not the same problem as the misses after starting in the ride the day before.
This event was an engine miss, pure and simple: the spark happened, but there was no bang!

Maybe it's time to check my plugs.
Honestly, I can't remember replacing them.

Ever.

## Wrap

So there it is.
After 20-some years of off and on development, I finally have my log visualizer.

I have had a lot of dreams for this project over the years, and I can say it is really nice to get to this point.

I had always dreamed of getting this system out for a track day.
Truthfully, it's taken so long to get all this working that I think my track days are over.
It's a bit of a bummer because I would have loved to have a log of me going around Laguna Seca, immortalized as a couple hundred megabytes of data.

Oh well.
I would have been going slowly, of course.
I always rode in the 'B' group with the rest of the slow guys.
Before I got the Tuono, I took my Yamaha V-Max to a class at Laguna Seca.
It was a great day:
![vmax-at-laguna-seca](images/laguna-seca.jpg)

The best part of that day?
Passing some dude riding his Aprilia Mille in turn 3!
Proof that a 1985 V-Max can turn.
But honestly, they don't really like turning.

## Acknowledgements

As mentioned earlier, I have wanted a visualizer for years.
What held me back was the prospect of spending months or years figuring out how to learn to work with all the graphics systems.
It is not something that I want to spend my limited brainpower on.
The big change came by treating the vizualizer as an experiment to see if I could get it built in conjunction with the AI service, Claude Code.

I will say this: the experiment was certainly not without its problems and rat holes, but on the plus side, this whole visualizer only took about 2 weeks to get to its current state.
Maybe AI is good for something!

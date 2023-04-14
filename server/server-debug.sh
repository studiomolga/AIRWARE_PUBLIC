
# set session name
SESSION="aware-server-debug"
SESSIONEXISTS=$(tmux list-sessions | grep $SESSION)

if [ "$SESSIONEXISTS" = "" ]
then

    # start a new session
    tmux new-session -d -s $SESSION

    tmux rename-window -t 0 'Server debug'
    tmux send-keys -t 'Server debug' 'ssh pi@192.168.1.11' C-m 'tail -f /home/pi/AIRWARE_PUBLIC/server/mqtt_server/awair_server.log' C-m

    tmux select-window 'Server debug'
    #tmux new-window -t $SESSION:1 -n 'New server'
    tmux split-window -h
    tmux send-keys -t 'Server debug' 'ssh pi@192.168.1.11' C-m 'tail -f /home/pi/aware_test/AIRWARE_PUBLIC/server/mqtt_server/awair_server.log' C-m
fi

tmux attach-session -t $SESSION:0

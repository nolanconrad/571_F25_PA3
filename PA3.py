import sys

#CONSTANTS
FREQUENCIES = [1188, 918, 648, 384]  # in MHz
testMode = 1

def main():
    # ARGUMENTS are file name, scheduling stratgegy, energy efficient (optional)
    if testMode == 1:
        input_file = "test_input.txt"
        algorithm = "EDF"
        energy_efficient = False
    else:
        if len(sys.argv)<3 or sys.argv[2] not in ("EDF","RM"):
                print("Usage: python PA3.py <input_file> <EDF|RM> [EE]")
                sys.exit(1)
        input_file = sys.argv[1]
        algorithm = sys.argv[2]
        if(len(sys.argv) == 4 and sys.argv[3] == "EE"):
            energy_efficient = True
        else:
            energy_efficient = False

    if(testMode == 1):
        print(parseInput(input_file))


    file = parseInput(input_file)

    
def parseInput(input_file):
    #  file is space delimited text file. 
    #  first line is:
        #  1 - numTasks,
        #  2 - time the system will execute in seconds,
        #  3 - active CPU power at 1188 MHz,
        #  4 - active CPU power at 918 MHz, 
        #  5 - active CPU power at 648 Mhz
        #  6 - idle CPU power at lowest frequency (384 MHz),
    #  subsequent lines are:
        #  1 - task name,
        #  2 - period/ deadline,
        #  3 - WCET at 1188 MHz,
        #  4 - WCET at 918 MHz,
        #  5 - WCET at 648 MHz,
        #  6 - WCET at 384 MHz
    with open(input_file, 'r') as f:
        lines = f.readlines()
    header = lines[0].strip().split()
    num_tasks = int(header[0])
    T_end = int(header[1])
    power = {
        1188: int(header[2]),
        918: int(header[3]),
        648: int(header[4]),
        384: int(header[5])
    }
    idle_power = int(header[6])
    tasks = []
    for line in lines[1:num_tasks+1]:
        parts = line.strip().split()
        name = parts[0]
        period = int(parts[1])
        wcet = {
            1188: int(parts[2]),
            918: int(parts[3]),
            648: int(parts[4]),
            384: int(parts[5])
        }
        tasks.append({
            'name': name,
            'period': period,
            'wcet': wcet
        })
    return {
        'num_tasks': num_tasks,
        'T_end': T_end,
        'power': power,
        'idle_power': idle_power,
        'tasks': tasks
    }

if __name__ == "__main__":
    main()


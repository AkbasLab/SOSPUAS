import pygame
from pygame.locals import *
import glm

from OpenGL.GL import *
from OpenGL.GLU import *

import operator
import time

verticies = (
    glm.vec3(1, -1, -1),
    glm.vec3(1, 1, -1),
    glm.vec3(-1, 1, -1),
    glm.vec3(-1, -1, -1),
    glm.vec3(1, -1, 1),
    glm.vec3(1, 1, 1),
    glm.vec3(-1, -1, 1),
    glm.vec3(-1, 1, 1)
    )

edges = (
    (0,1),
    (0,3),
    (0,4),
    (2,1),
    (2,3),
    (2,7),
    (6,3),
    (6,4),
    (6,7),
    (5,1),
    (5,4),
    (5,7)
    )


def render_cube(offset=glm.vec3(0,0,0), size=1.0):
    glBegin(GL_LINES)
    for edge in edges:
        for vertex in edge:
            tmp = verticies[vertex] * size / 2.0 + offset
            glVertex3f(tmp.x, tmp.y, tmp.z)
    glEnd()


def lerp(a, b, f):
    # Convert the 0-1 range into a value in the right range.
    return a + ((b - a) * f)


def normalize(a, b, value):
    return float(value - a) / float(b - a)


def map(value, leftMin, leftMax, rightMin, rightMax):
    # Figure out how 'wide' each range is
    f = normalize(leftMin, leftMax, value)

    return lerp(rightMin, rightMax, f)

def parse_csv_line(line):
    parts = line.split(",")

    result = {}
    result["time"] = float(parts[0])
    result["node_address"] = parts[1]
    result["ip_address"] = parts[2]
    result["x"] = float(parts[3])
    result["y"] = float(parts[4])
    result["z"] = float(parts[5])
    return result


def main():
    file_name = "positions.csv"
    csv_file = open(file_name, "r")
    raw_lines = csv_file.read().splitlines()
    csv_file.close()
    csv_header = raw_lines[0]

    lines = []
    for line in raw_lines[1:]:
        #Parse raw lines and stick into lines
        parsed = parse_csv_line(line)
        parsed["pos"] = glm.vec3(parsed["x"], parsed["y"], parsed["z"])
        lines.append(parsed)

    uavs = set()
    for line in lines:
        uavs.add(line["node_address"])

    print("Loaded {} uav's from file {}".format(len(uavs), file_name))

    #Mapping between a uav id and the index of the latest line that assert's a uav's position that is before the current re-simulation's time
    #Used for interpolating each uav's position each frame
    uav_last_index = {}

    for uav in uavs:
        #Start searching for times at the start of the file
        uav_last_index[uav] = -0



    pygame.init()
    display = (1440, 810)
    pygame.display.set_mode(display, DOUBLEBUF | OPENGL)
    clock=pygame.time.Clock()
    pygame.mouse.set_visible(False)
    pygame.event.set_grab(True)

    target_fps = 144
    simulation_speed = 1.0
    sensitivity = 0.75 * 1.0 / target_fps
    
    move_speed = 15.0 * 1.0 / target_fps

    gluPerspective(70, (display[0]/display[1]), 0.01, 500.0)

    #The position of the camera in 3d space
    camera_pos = glm.vec3(0.0, 0.0, 10.0)

    #A unit vector pointing where the camera is looking - relative to the position of the camera
    camera_forward = glm.vec3(0.0, 0.0, -1.0)
    keymap = {}

    for i in range(0, 256):
        keymap[i] = False

    last_time = time.time()
    delta_time = 0.0
    simulation_time = 0.0
    while True:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                quit()
            camera_up = glm.vec3(0.0, 1.0, 0.0)
            camera_right = glm.cross(camera_forward, camera_up)
            if event.type == pygame.MOUSEMOTION:
                mouse_move = pygame.mouse.get_rel()
                camera_forward = glm.rotate(camera_forward, -mouse_move[0] * sensitivity, camera_up)
                camera_forward = glm.rotate(camera_forward, -mouse_move[1] * sensitivity, camera_right)
                #cameraLook.rotate(

            if event.type == pygame.KEYDOWN:
                keymap[event.scancode] = True
                if (event.scancode == pygame.KSCAN_ESCAPE):
                    quit()

            if event.type == pygame.KEYUP:
                keymap[event.scancode] = False

        camera_move = glm.vec3(0.0)
        if keymap[pygame.KSCAN_W]:
            camera_move += camera_forward
        
        if keymap[pygame.KSCAN_S]:
            camera_move -= camera_forward
        
        if keymap[pygame.KSCAN_A]:
            camera_move -= camera_right
        
        if keymap[pygame.KSCAN_D]:
            camera_move += camera_right
         
        if keymap[pygame.KSCAN_SPACE]:
            camera_move += camera_up
          
        if keymap[pygame.KSCAN_LSHIFT]:
            camera_move -= camera_up

        if glm.length(camera_move) > 0.0:
            camera_pos += glm.normalize(camera_move) * move_speed
    

        glPushMatrix()

        #glRotatef(1, 3, 1, 1)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

        gluLookAt(camera_pos.x, camera_pos.y, camera_pos.z, camera_pos.x + camera_forward.x, camera_pos.y + camera_forward.y, camera_pos.z + camera_forward.z, 0.0, 1.0, 0.0)
        for uav in uavs:
            #We need to find 2 lines in the list of all lines that tell us this uav's position before and after this re rendering's time step
            #We will the interpolate these two time points with the uav's position and our middle time in this rendering, allowing us
            #to have a smooth re-rendering of the swarm
            search_index = uav_last_index[uav]
            #We need to find the latest time a position was set in the csv file that is before _simulation_time_.
            #If this doesn't exist then we skip this uav since it doesn't have a position yet

            before_index = None
            after_index = None
            while search_index < len(lines):
                data = lines[search_index]
                if data["node_address"] == uav:
                    #We found a line that talks about this uav
                    if data["time"] <= simulation_time:
                        before_index = search_index
                    else:
                        after_index = search_index
                        break
                search_index += 1

            if before_index != None:
                uav_last_index[uav] = before_index
            else:
                uav_last_index[uav] = search_index

            #print("Before {}, after {}".format(before_index, after_index))

            if before_index == None and after_index == None:
                #We have no position data nothing to do
                continue
            elif before_index != None and after_index == None:
                #There are no more data points after this point in the simulation. Lock uav's to thier last known position
                pos = lines[before_index]["pos"]
            elif before_index == None and after_index != None:
                #There are no data points before here so for all we know the UAV was always here (should be unlikely in pratice)
                pos = lines[after_index]["pos"]
            else:
                #We have data points before and after so interpolate!
                a = lines[before_index]["pos"]
                a_time = lines[before_index]["time"]
                b = lines[after_index]["pos"]
                b_time = lines[after_index]["time"]
                f = normalize(a_time, b_time, simulation_time)
                pos = lerp(a, b, f)
                #print("sim {}, a is {}, b is {}, f {}".format(simulation_time, a_time, b_time, f))

            render_cube(offset=pos, size=0.5)
            


        pygame.display.flip()

        glPopMatrix()

        clock.tick(target_fps)
        now = time.time()
        delta_time = now - last_time
        last_time = now

        simulation_time += delta_time * simulation_speed
        #print("time at {}, delta {}".format(simulation_time, delta_time))


main()


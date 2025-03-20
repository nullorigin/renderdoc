import renderdoc as rd
import rdtest
from rdtest import analyse

class VK_Mesh_Shader(rdtest.TestCase):
    demos_test_name = 'VK_Mesh_Shader'
    demos_frame_cap = 5

    def build_local_taskout_reference(self):
        reference = {}
        reference[0] = { 'tri': [0, 1, 2, 3] }
        return reference

    def build_meshout_reference(self, orgY, color):
        countTris = 4 
        triSize = 0.2
        deltX = 0.42
        orgX = -0.65
        i = 0
        reference = {}
        for tri in range(countTris):
            for vert in range(3):
                posX = orgX + tri * deltX
                posY = orgY

                if vert == 0:
                    posX += -triSize
                    posY += -triSize
                elif vert == 1:
                    posX += 0.0
                    posY += triSize
                elif vert == 2:
                    posX += triSize
                    posY += -triSize

                reference[i] = {
                    'vtx': i,
                    'idx': i,
                    'gl_Position': [posX, posY, 0.0, 1.0],
                    'outColor': color,
                }
                i += 1
        return reference

    def check_capture(self):
        last_action: rd.ActionDescription = self.get_last_action()

        self.controller.SetFrameEvent(last_action.eventId, True)

        action = self.find_action("Mesh Shaders")

        action = action.next
        name = f"Pure Mesh Shader Test EID:{action.eventId}"
        rdtest.log.begin_section(name)
        self.controller.SetFrameEvent(action.eventId, False)

        x = 70
        y = 240
        
        orgY = 0.65
        color = [1.0, 0.0, 0.0, 1.0]
        postms_ref = self.build_meshout_reference(orgY, color)
        postms_data = self.get_postvs(action, rd.MeshDataStage.MeshOut, 0, action.numIndices)
        self.check_mesh_data(postms_ref, postms_data)
        self.check_debug_pixel(x, y)
        rdtest.log.end_section(name)

        y -= 100
        action = action.next
        name = f"Amplification Shader with Local Payload EID:{action.eventId}"
        rdtest.log.begin_section(name)
        self.controller.SetFrameEvent(action.eventId, False)

        postts_ref = self.build_local_taskout_reference()
        postts_data = self.get_task_data(action)
        self.check_task_data(postts_ref, postts_data)

        orgY = 0.0
        color = [0.0, 0.0, 1.0, 1.0]
        postms_ref = self.build_meshout_reference(orgY, color)
        postms_data = self.get_postvs(action, rd.MeshDataStage.MeshOut, 0, action.numIndices)
        self.check_mesh_data(postms_ref, postms_data)
        self.check_debug_pixel(x, y)
        rdtest.log.end_section(name)
